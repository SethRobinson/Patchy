#include "color/color_management.hpp"
#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "core/vector_shape.hpp"
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
#include "psd/psd_io_internal.hpp"
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
using patchy::test::psd_layer_extra_data;
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

// Photoshop reads a pixel record without a transparency channel as its Background layer,
// opaque over the whole canvas whatever its bounds. September 2026: an imported opaque photo
// (an RGB layer) above other layers blanked everything beneath it in Photoshop. Only the
// bottom record covering exactly the canvas may omit the channel.
void psd_opaque_rgb_layers_write_transparency_unless_background() {
  const auto channel_ids = [](const patchy::Document& document) {
    std::vector<std::int16_t> ids;
    for (const auto& record : psd_layer_channel_records(patchy::psd::DocumentIo::write_layered_rgb8(document))) {
      ids.push_back(record.id);
    }
    return ids;
  };

  patchy::Document stacked(8, 6, patchy::PixelFormat::rgb8());
  stacked.add_pixel_layer("Background", solid_rgb(8, 6, 250, 240, 20));
  patchy::Layer photo(stacked.allocate_layer_id(), "Photo", solid_rgb(4, 3, 10, 20, 30));
  photo.set_bounds(patchy::Rect{2, 1, 4, 3});
  stacked.add_layer(std::move(photo));
  CHECK((channel_ids(stacked) == std::vector<std::int16_t>{0, 1, 2, 0, 1, 2, -1}));

  const auto read = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(stacked));
  CHECK(read.layers().size() == 2U);
  const auto& read_photo = read.layers().back();
  CHECK(read_photo.pixels().format().channels == 4U);
  CHECK(read_photo.pixels().pixel(3, 2)[3] == 255U);
  CHECK(read_photo.pixels().pixel(3, 2)[0] == 10U);

  // A bottom layer that does not cover the canvas is not a Background either.
  patchy::Document partial(8, 6, patchy::PixelFormat::rgb8());
  patchy::Layer lone(partial.allocate_layer_id(), "Photo", solid_rgb(4, 3, 10, 20, 30));
  lone.set_bounds(patchy::Rect{2, 1, 4, 3});
  partial.add_layer(std::move(lone));
  CHECK((channel_ids(partial) == std::vector<std::int16_t>{0, 1, 2, -1}));
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

void psd_group_layer_mask_round_trips() {
  patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(4, 2, 255, 255, 255));

  patchy::Layer group(document.allocate_layer_id(), "Masked Folder", patchy::LayerKind::Group);
  group.add_child(patchy::Layer(document.allocate_layer_id(), "Red Child", solid_rgba(4, 2, 220, 20, 20, 255)));
  patchy::PixelBuffer mask_pixels(2, 2, patchy::PixelFormat::gray8());
  mask_pixels.clear(255);
  group.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 2, 2}, std::move(mask_pixels), 0, false});
  patchy::set_layer_mask_linked(group, false);
  document.add_layer(std::move(group));

  // The mask (white 2x2 over default black) confines the group's child.
  auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(0, 0)[0] == 220);
  CHECK(flattened.pixel(3, 0)[0] == 255);

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2);
  const auto& folder = read.layers()[1];
  CHECK(folder.kind() == patchy::LayerKind::Group);
  CHECK(folder.mask().has_value());
  CHECK(folder.mask()->bounds.x == 0);
  CHECK(folder.mask()->bounds.width == 2);
  CHECK(folder.mask()->bounds.height == 2);
  CHECK(folder.mask()->default_color == 0);
  CHECK(!folder.mask()->disabled);
  CHECK(*folder.mask()->pixels.pixel(1, 1) == 255);
  CHECK(!patchy::layer_mask_linked(folder));

  flattened = patchy::Compositor{}.flatten_rgb8(read);
  CHECK(flattened.pixel(0, 0)[0] == 220);
  CHECK(flattened.pixel(3, 0)[0] == 255);

  // Second generation: a Patchy-written masked group re-reads identically.
  const auto read_again = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(read));
  CHECK(read_again.layers()[1].mask().has_value());
  CHECK(*read_again.layers()[1].mask()->pixels.pixel(0, 0) == 255);
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

// A flattened PSD in any color mode, built by hand: header, color mode data, no image
// resources, no layers, then raw composite planes.
std::vector<std::uint8_t> flat_psd_bytes(std::uint16_t channels, std::uint32_t width, std::uint32_t height,
                                         std::uint16_t depth, std::uint16_t color_mode,
                                         const std::vector<std::uint8_t>& color_mode_data,
                                         const std::vector<std::uint8_t>& planes) {
  std::vector<std::uint8_t> bytes{'8', 'B', 'P', 'S', 0, 1, 0, 0, 0, 0, 0, 0};
  const auto u16 = [&bytes](std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value));
  };
  const auto u32 = [&bytes](std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
      bytes.push_back(static_cast<std::uint8_t>(value >> static_cast<unsigned>(shift)));
    }
  };
  u16(channels);
  u32(height);
  u32(width);
  u16(depth);
  u16(color_mode);
  u32(static_cast<std::uint32_t>(color_mode_data.size()));
  bytes.insert(bytes.end(), color_mode_data.begin(), color_mode_data.end());
  u32(0);  // image resources
  u32(0);  // layer and mask information
  u16(0);  // raw composite
  bytes.insert(bytes.end(), planes.begin(), planes.end());
  return bytes;
}

// Bitmap, Indexed, Duotone, Lab and Multichannel documents open by converting to RGB at
// read time, like CMYK and Grayscale do.
void psd_other_color_modes_convert_to_rgb_on_read() {
  const auto background_pixel = [](const patchy::Document& document, std::int32_t x, std::int32_t y) {
    const auto& pixels = document.layers().front().pixels();
    const auto* pixel = pixels.pixel(x, y);
    return std::array<int, 3>{pixel[0], pixel[1], pixel[2]};
  };
  const auto close_to = [](const std::array<int, 3>& actual, int red, int green, int blue, int tolerance) {
    return std::abs(actual[0] - red) <= tolerance && std::abs(actual[1] - green) <= tolerance &&
           std::abs(actual[2] - blue) <= tolerance;
  };

  // Bitmap: 1 bit per pixel, rows packed to bytes, a set bit is black. 10 x 2 pixels.
  {
    const auto document = patchy::psd::DocumentIo::read(
        flat_psd_bytes(1, 10, 2, 1, 0, {}, {0b10100000, 0b01000000, 0b00000000, 0b11000000}));
    CHECK(document.width() == 10 && document.height() == 2);
    CHECK(document.layers().size() == 1);
    CHECK(close_to(background_pixel(document, 0, 0), 0, 0, 0, 0));
    CHECK(close_to(background_pixel(document, 1, 0), 255, 255, 255, 0));
    CHECK(close_to(background_pixel(document, 2, 0), 0, 0, 0, 0));
    CHECK(close_to(background_pixel(document, 9, 0), 0, 0, 0, 0));    // second byte, second bit
    CHECK(close_to(background_pixel(document, 8, 0), 255, 255, 255, 0));
    CHECK(close_to(background_pixel(document, 0, 1), 255, 255, 255, 0));
    CHECK(close_to(background_pixel(document, 8, 1), 0, 0, 0, 0));
    CHECK(document.metadata().values.at("psd.color_mode") == "Bitmap");
  }
  // Indexed: one plane of indices into the 768-byte table (reds, greens, blues).
  {
    std::vector<std::uint8_t> palette(768, 0);
    palette[1] = 200;          // entry 1 red
    palette[256 + 1] = 100;    // entry 1 green
    palette[512 + 1] = 50;     // entry 1 blue
    palette[255] = 10;
    palette[256 + 255] = 20;
    palette[512 + 255] = 30;
    const auto document = patchy::psd::DocumentIo::read(flat_psd_bytes(1, 3, 1, 8, 2, palette, {1, 0, 255}));
    CHECK(close_to(background_pixel(document, 0, 0), 200, 100, 50, 0));
    CHECK(close_to(background_pixel(document, 1, 0), 0, 0, 0, 0));
    CHECK(close_to(background_pixel(document, 2, 0), 10, 20, 30, 0));
  }
  // Duotone: read as its gray plane.
  {
    const auto document = patchy::psd::DocumentIo::read(flat_psd_bytes(1, 2, 1, 8, 8, {}, {0, 200}));
    CHECK(close_to(background_pixel(document, 0, 0), 0, 0, 0, 0));
    CHECK(close_to(background_pixel(document, 1, 0), 200, 200, 200, 0));
  }
  // Lab: L 0..255 = 0..100, a and b offset by 128. White, black, mid gray, and a
  // saturated red (L 54, a +81, b +70 is close to sRGB 255, 0, 0).
  {
    const auto document = patchy::psd::DocumentIo::read(flat_psd_bytes(
        3, 4, 1, 8, 9, {}, {255, 0, 136, 138, 128, 128, 128, 209, 128, 128, 128, 198}));
    CHECK(close_to(background_pixel(document, 0, 0), 255, 255, 255, 1));
    CHECK(close_to(background_pixel(document, 1, 0), 0, 0, 0, 1));
    const auto gray = background_pixel(document, 2, 0);
    CHECK(std::abs(gray[0] - gray[1]) <= 1 && std::abs(gray[1] - gray[2]) <= 1);
    CHECK(gray[0] > 118 && gray[0] < 136);  // L 53.3 is sRGB gray 127
    CHECK(close_to(background_pixel(document, 3, 0), 255, 0, 0, 12));
  }
  // Multichannel: ink planes, 255 = none; the first three read as cyan, magenta, yellow.
  {
    const auto document =
        patchy::psd::DocumentIo::read(flat_psd_bytes(3, 2, 1, 8, 7, {}, {0, 255, 255, 255, 255, 0}));
    CHECK(close_to(background_pixel(document, 0, 0), 0, 255, 255, 0));   // full cyan ink
    CHECK(close_to(background_pixel(document, 1, 0), 255, 255, 0, 0));   // full yellow ink
  }
  // A 1-bit file that does not claim Bitmap mode is still refused.
  bool refused = false;
  try {
    (void)patchy::psd::DocumentIo::read(flat_psd_bytes(3, 2, 1, 1, 3, {}, {0, 0, 0}));
  } catch (const std::exception&) {
    refused = true;
  }
  CHECK(refused);
}

// The psd-tools files in those modes all open (testy/fetch_psd_tools_corpus.py).
void psd_tools_other_color_modes_open_if_available() {
  const auto root = patchy::test::source_root_path() / "local-test-fixtures" / "psd-tools" / "tests" / "psd_files";
  if (!std::filesystem::exists(root / "colormodes" / "4x4_8bit_lab.psd")) {
    std::cout << "[SKIP] psd-tools collection missing: " << root.string() << '\n';
    return;
  }
  const std::array<std::pair<const char*, const char*>, 11> files{{
      {"colormodes/100x20_1bit_bitmap_rle.psd", "Bitmap"},
      {"colormodes/20x5_1bit_bitmap.psd", "Bitmap"},
      {"colormodes/4x4_1bit_bitmap.psd", "Bitmap"},
      {"colormodes/4x4_8bit_index_color.psd", "Indexed"},
      {"colormodes/4x4_8bit_duotone.psd", "Duotone"},
      {"colormodes/4x4_8bit_lab.psd", "Lab"},
      {"colormodes/4x4_16bit_lab.psd", "Lab"},
      {"colormodes/4x4_16bit_multichannel.psd", "Multichannel"},
      {"descriptors/lab-color-swatches.psd", "Lab"},
      {"descriptors/stroke-color-descriptors-lab.psd", "Lab"},
      {"gradients/noise-gradient-lab.psd", "Lab"},
  }};
  for (const auto& [relative, mode] : files) {
    try {
      const auto document = patchy::psd::DocumentIo::read_file(root / relative);
      CHECK(document.width() > 0 && document.height() > 0);
      CHECK(!document.layers().empty());
      CHECK(document.metadata().values.at("psd.color_mode") == mode);
      const auto flattened = patchy::flatten_document_rgba8(document);
      CHECK(flattened.width() == document.width());
    } catch (const std::exception& error) {
      std::cout << "[INFO] " << relative << " refused: " << error.what() << '\n';
      CHECK(false);
    }
  }
  // The RLE bitmap is a 100 x 20 picture with both colors in it.
  const auto bitmap = patchy::psd::DocumentIo::read_file(root / "colormodes" / "100x20_1bit_bitmap_rle.psd");
  const auto& pixels = bitmap.layers().front().pixels();
  bool has_black = false;
  bool has_white = false;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      has_black = has_black || pixels.pixel(x, y)[0] == 0;
      has_white = has_white || pixels.pixel(x, y)[0] == 255;
    }
  }
  CHECK(has_black && has_white);
}

// Adjustment layers of a CMYK document act on the inks. Photoshop 2026's flatten of
// psd-tools' levels_cmyk.psd (four masked Levels layers, each with a black-ink record,
// over a photo and four color ramps; U.S. Web Coated (SWOP) v2 embedded), sampled in each
// strip. Running the same records on RGB, as Patchy did, is off by 60 and more here.
void psd_tools_cmyk_levels_run_on_the_inks_if_available() {
  const auto path = patchy::test::source_root_path() / "local-test-fixtures" / "psd-tools" / "tests" /
                    "psd_files" / "adjustments" / "levels_cmyk.psd";
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] psd-tools collection missing: " << path.string() << '\n';
    return;
  }
  const auto document = patchy::psd::DocumentIo::read_file(path);
  int ink_adjustments = 0;
  const std::function<void(const std::vector<patchy::Layer>&)> count = [&](const std::vector<patchy::Layer>& layers) {
    for (const auto& layer : layers) {
      if (const auto settings = patchy::adjustment_settings_from_layer(layer); settings.has_value()) {
        CHECK(settings->kind == patchy::AdjustmentKind::Levels);
        CHECK(settings->ink_space != nullptr);
        CHECK(patchy::adjustment_runs_in_ink_space(*settings));
        // No per-channel RGB table exists for an ink-space adjustment.
        CHECK(!patchy::build_adjustment_lut(*settings).has_value());
        ++ink_adjustments;
      }
      count(layer.children());
    }
  };
  count(document.layers());
  CHECK(ink_adjustments == 4);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  struct Probe {
    std::int32_t x;
    std::int32_t y;
    int red;
    int green;
    int blue;
  };
  const std::array<Probe, 16> photoshop{{
      {25, 40, 0, 44, 40},      {25, 100, 0, 130, 117},    {25, 150, 0, 146, 131},   {25, 185, 237, 28, 43},
      {75, 40, 179, 184, 219},  {75, 100, 252, 241, 247},  {75, 150, 106, 109, 164}, {75, 185, 238, 29, 37},
      {125, 40, 213, 178, 175}, {125, 100, 193, 167, 170}, {125, 150, 114, 82, 73},  {125, 185, 243, 108, 34},
      {175, 40, 67, 54, 55},    {175, 100, 167, 134, 141}, {175, 150, 76, 43, 40},   {175, 185, 239, 64, 47},
  }};
  int worst = 0;
  for (const auto& probe : photoshop) {
    const auto* px = flattened.pixel(probe.x, probe.y);
    const auto miss = std::max({std::abs(px[0] - probe.red), std::abs(px[1] - probe.green),
                                std::abs(px[2] - probe.blue)});
    if (miss > 12) {
      std::cout << "[INFO] levels_cmyk " << probe.x << "," << probe.y << " is " << int{px[0]} << ", " << int{px[1]}
                << ", " << int{px[2]} << " (Photoshop " << probe.red << ", " << probe.green << ", " << probe.blue
                << ")\n";
    }
    worst = std::max(worst, miss);
  }
  std::cout << "[INFO] levels_cmyk worst channel miss against Photoshop: " << worst << '\n';
  CHECK(worst <= 12);

  // Saved as RGB, the Levels layers stay Levels layers, and each one's fifth record (the
  // black ink) is written as the identity: Photoshop 2026 discards a Levels layer of an
  // RGB document whose fifth record is anything else, leaving a plain empty layer.
  const auto saved = patchy::psd::DocumentIo::write_layered_rgb8(document);
  int levels_blocks = 0;
  const std::array<std::uint8_t, 8> key{'8', 'B', 'I', 'M', 'l', 'e', 'v', 'l'};
  for (auto at = std::search(saved.begin(), saved.end(), key.begin(), key.end()); at != saved.end();
       at = std::search(at + 1, saved.end(), key.begin(), key.end())) {
    const auto payload = static_cast<std::size_t>(at - saved.begin()) + 12U;
    const auto u16 = [&saved](std::size_t offset) { return (saved[offset] << 8) | saved[offset + 1U]; };
    const auto fifth = payload + 2U + 4U * 10U;
    CHECK(u16(fifth) == 0 && u16(fifth + 2U) == 255 && u16(fifth + 4U) == 0 && u16(fifth + 6U) == 255 &&
          u16(fifth + 8U) == 100);
    ++levels_blocks;
  }
  CHECK(levels_blocks == 4);
  int reread_levels = 0;
  const auto reread_document = patchy::psd::DocumentIo::read(saved);
  const std::function<void(const std::vector<patchy::Layer>&)> count_levels = [&](const std::vector<patchy::Layer>& layers) {
    for (const auto& layer : layers) {
      if (const auto settings = patchy::adjustment_settings_from_layer(layer);
          settings.has_value() && settings->kind == patchy::AdjustmentKind::Levels) {
        CHECK(layer.mask().has_value());
        ++reread_levels;
      }
      count_levels(layer.children());
    }
  };
  count_levels(reread_document.layers());
  CHECK(reread_levels == 4);

  // A layer that loses its ink space (the id is not registered in this process) falls
  // back to RGB math instead of failing.
  auto orphan = patchy::Layer(0, "orphan", patchy::LayerKind::Adjustment);
  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Levels;
  settings.levels.gamma_percent = 150;
  patchy::configure_adjustment_layer(orphan, settings);
  orphan.metadata()[patchy::kLayerMetadataAdjustmentInkSpace] = "cmyk-0-0";
  const auto reread = patchy::adjustment_settings_from_layer(orphan);
  CHECK(reread.has_value() && reread->ink_space == nullptr);
  CHECK(reread.has_value() && !patchy::adjustment_runs_in_ink_space(*reread));
}

// A grayscale document's Levels record and curve sit in the slot an RGB document calls red.
// Applied to red alone they tinted the picture; they belong to the one gray channel.
// Photoshop 2026's flatten of psd-tools' levels_grayscale.psd and curves_grayscale.psd
// (four masked adjustment layers each), sampled in every strip.
void psd_tools_grayscale_adjustments_apply_to_the_gray_channel_if_available() {
  const auto root = patchy::test::source_root_path() / "local-test-fixtures" / "psd-tools" / "tests" /
                    "psd_files" / "adjustments";
  struct Probe {
    std::int32_t x;
    std::int32_t y;
    int gray;
  };
  struct Case {
    const char* name;
    std::array<Probe, 16> photoshop;
  };
  // (Threshold too: four layers with four different levels, which also pins that each
  // layer gets its own table. One table reused for all of them turned the picture white.)
  const std::array<Case, 3> cases{{
      {"threshold_grayscale.psd",
       {{{25, 40, 0}, {25, 100, 255}, {25, 150, 255}, {25, 185, 255}, {75, 40, 255}, {75, 100, 255}, {75, 150, 0},
         {75, 185, 0}, {125, 40, 0}, {125, 100, 0}, {125, 150, 0}, {125, 185, 0}, {175, 40, 0}, {175, 100, 0},
         {175, 150, 0}, {175, 185, 0}}}},
      {"levels_grayscale.psd",
       {{{25, 40, 3}, {25, 100, 67}, {25, 150, 85}, {25, 185, 58}, {75, 40, 181}, {75, 100, 235}, {75, 150, 81},
         {75, 185, 160}, {125, 40, 175}, {125, 100, 166}, {125, 150, 138}, {125, 185, 164}, {175, 40, 37},
         {175, 100, 131}, {175, 150, 36}, {175, 185, 160}}}},
      {"curves_grayscale.psd",
       {{{25, 40, 42}, {25, 100, 90}, {25, 150, 103}, {25, 185, 84}, {75, 40, 142}, {75, 100, 198}, {75, 150, 78},
         {75, 185, 125}, {125, 40, 205}, {125, 100, 186}, {125, 150, 123}, {125, 185, 182}, {175, 40, 70},
         {175, 100, 158}, {175, 150, 44}, {175, 185, 166}}}},
  }};
  for (const auto& entry : cases) {
    const auto path = root / entry.name;
    if (!std::filesystem::exists(path)) {
      std::cout << "[SKIP] psd-tools collection missing: " << path.string() << '\n';
      return;
    }
    const auto flattened = patchy::Compositor{}.flatten_rgb8(patchy::psd::DocumentIo::read_file(path));
    int worst = 0;
    int worst_tint = 0;
    int far_off = 0;
    for (const auto& probe : entry.photoshop) {
      const auto* px = flattened.pixel(probe.x, probe.y);
      worst_tint = std::max({worst_tint, std::abs(px[0] - px[1]), std::abs(px[1] - px[2])});
      const auto miss = std::max({std::abs(px[0] - probe.gray), std::abs(px[1] - probe.gray),
                                  std::abs(px[2] - probe.gray)});
      worst = std::max(worst, miss);
      far_off += miss > 12 ? 1 : 0;
    }
    std::cout << "[INFO] " << entry.name << " worst miss against Photoshop " << worst << ", worst tint "
              << worst_tint << '\n';
    CHECK(worst_tint == 0);
    // A threshold is all or nothing per pixel, so a probe beside an edge may flip; the
    // other files are smooth and every probe has to be close.
    CHECK(std::string_view(entry.name).starts_with("threshold") ? far_off <= 1 : worst <= 12);
  }
}

// A gradient fill layer whose descriptor has no angle runs left to right. The noise fills
// of psd-tools' gradient-styles.psd omit it; at the effect default of 90 degrees their
// vertical bands were redrawn as horizontal ones.
void psd_tools_gradient_fill_without_angle_runs_left_to_right_if_available() {
  const auto path = patchy::test::source_root_path() / "local-test-fixtures" / "psd-tools" / "tests" /
                    "psd_files" / "gradient-styles.psd";
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] psd-tools collection missing: " << path.string() << '\n';
    return;
  }
  const auto document = patchy::psd::DocumentIo::read_file(path);
  int noise = 0;
  int angled = 0;
  const std::function<void(const std::vector<patchy::Layer>&)> visit = [&](const std::vector<patchy::Layer>& layers) {
    for (const auto& layer : layers) {
      visit(layer.children());
      const auto* shape = layer.vector_shape();
      if (shape == nullptr || shape->fill.kind != patchy::VectorFillKind::Gradient) {
        continue;
      }
      if (layer.name().starts_with("Roughness")) {
        CHECK(shape->fill.gradient.angle_degrees == 0.0F);
        // Bands that run down the layer: one row is the same color all the way across
        // only for a horizontal ramp's transpose, so compare along a column instead.
        const auto& pixels = layer.pixels();
        if (pixels.width() > 8 && pixels.height() > 8) {
          const auto* top = pixels.pixel(pixels.width() / 2, 1);
          const auto* bottom = pixels.pixel(pixels.width() / 2, pixels.height() - 2);
          CHECK(std::abs(top[0] - bottom[0]) <= 8 && std::abs(top[1] - bottom[1]) <= 8);
        }
        ++noise;
      } else if (shape->fill.gradient.angle_degrees != 0.0F) {
        ++angled;
      }
    }
  };
  visit(document.layers());
  CHECK(noise == 5);
  CHECK(angled == 15);  // the fills that do carry an angle keep it
}

// A Lab-mode document writes every color as a Lab descriptor ('LbCl'). Read as RGB, each
// fill layer's color came out black, which showed as soon as the layer was redrawn. Each
// solid fill of psd-tools' lab-color-swatches.psd has to parse to the color Photoshop
// baked into that layer's own pixels.
void psd_tools_lab_color_descriptors_parse_if_available() {
  const auto path = patchy::test::source_root_path() / "local-test-fixtures" / "psd-tools" / "tests" /
                    "psd_files" / "descriptors" / "lab-color-swatches.psd";
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] psd-tools collection missing: " << path.string() << '\n';
    return;
  }
  const auto document = patchy::psd::DocumentIo::read_file(path);
  int fills = 0;
  int worst = 0;
  bool any_color = false;
  for (const auto& layer : document.layers()) {
    const auto* shape = layer.vector_shape();
    const auto& pixels = layer.pixels();
    if (shape == nullptr || pixels.empty()) {
      continue;
    }
    const auto* baked = pixels.pixel(pixels.width() / 2, pixels.height() / 2);
    const auto color = shape->fill.color;
    const auto miss = std::max({std::abs(color.red - baked[0]), std::abs(color.green - baked[1]),
                                std::abs(color.blue - baked[2])});
    if (miss > 14) {
      std::cout << "[INFO] " << layer.name() << " parsed " << int{color.red} << ", " << int{color.green} << ", "
                << int{color.blue} << " but Photoshop baked " << int{baked[0]} << ", " << int{baked[1]} << ", "
                << int{baked[2]} << '\n';
    }
    worst = std::max(worst, miss);
    any_color = any_color || color.red != color.green || color.green != color.blue;
    ++fills;
  }
  std::cout << "[INFO] lab-color-swatches: " << fills << " fills, worst channel miss " << worst << '\n';
  CHECK(fills >= 10);
  CHECK(any_color);
  // (The saturated yellow is outside sRGB; Photoshop and this conversion clip it a little
  // differently, 12/255 on blue. Everything else is within 3.)
  CHECK(worst <= 14);
}

// Reads every file of the psd-tools test collection (testy/fetch_psd_tools_corpus.py puts
// it under local-test-fixtures/psd-tools), then writes each document that loaded and reads
// the result back. A reader exception is a clean refusal and only counted (the collection
// holds deliberately damaged files); the test is for crashes, hangs, and documents
// Patchy accepts but cannot round-trip through its own writer.
void psd_tools_corpus_reads_and_round_trips_if_available() {
  const auto root =
      patchy::test::source_root_path() / "local-test-fixtures" / "psd-tools" / "tests" / "psd_files";
  if (!std::filesystem::exists(root)) {
    std::cout << "[SKIP] psd-tools collection missing: " << root.string() << '\n';
    return;
  }

  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    const auto extension = entry.path().extension();
    if (entry.is_regular_file() && (extension == ".psd" || extension == ".psb")) {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());

  int loaded = 0;
  int refused = 0;
  for (const auto& path : files) {
    const auto name = path.lexically_relative(root).generic_string();
    std::optional<patchy::Document> document;
    try {
      document = patchy::psd::DocumentIo::read_file(path);
    } catch (const std::exception& error) {
      ++refused;
      std::cout << "[INFO] psd-tools " << name << " refused: " << error.what() << '\n';
      continue;
    }
    ++loaded;
    CHECK(document->width() > 0);
    CHECK(document->height() > 0);

    try {
      patchy::psd::WriteOptions options;
      options.large_document = path.extension() == ".psb";
      const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(*document, options);
      const auto reread = patchy::psd::DocumentIo::read(bytes);
      const bool same_shape = reread.width() == document->width() && reread.height() == document->height() &&
                              reread.layers().size() == std::as_const(*document).layers().size();
      if (!same_shape) {
        std::cout << "[INFO] psd-tools " << name << " changed shape in a round trip\n";
      }
      CHECK(same_shape);
    } catch (const std::exception& error) {
      std::cout << "[INFO] psd-tools " << name << " failed its round trip: " << error.what() << '\n';
      CHECK(false);
    }
  }
  std::cout << "[INFO] psd-tools collection: " << files.size() << " files, " << loaded << " loaded, " << refused
            << " refused\n";
  CHECK(loaded > 0);
}

// Photoshop 2026's flatten of the psd-tools group Fill files: a blue layer inside a
// Fill 50% group over red is 127, 0, 128 whether the group is Normal or Pass Through.
void psd_tools_group_fill_matches_photoshop_if_available() {
  const auto root = patchy::test::source_root_path() / "local-test-fixtures" / "psd-tools" / "tests" / "psd_files" /
                    "transparency";
  for (const char* name : {"knockout-none-normal.psd", "knockout-none-passthrough.psd"}) {
    const auto path = root / name;
    if (!std::filesystem::exists(path)) {
      std::cout << "[SKIP] psd-tools collection missing: " << path.string() << '\n';
      return;
    }
    const auto document = patchy::psd::DocumentIo::read_file(path);
    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    const auto* px = flattened.pixel(16, 16);
    if (std::abs(px[0] - 127) > 1 || px[1] != 0 || std::abs(px[2] - 128) > 1) {
      std::cout << "[INFO] " << name << " center is " << int{px[0]} << ", " << int{px[1]} << ", " << int{px[2]}
                << '\n';
    }
    CHECK(std::abs(px[0] - 127) <= 1);
    CHECK(px[1] == 0);
    CHECK(std::abs(px[2] - 128) <= 1);
  }

  // A Pass Through group at Fill 40% holding a red layer and an Exposure adjustment: the
  // adjustment darkens the group's own content and leaves the backdrop alone.
  const auto adjustment_path = root.parent_path() / "passthrough_fill_adjustment.psd";
  if (!std::filesystem::exists(adjustment_path)) {
    return;
  }
  const auto flattened =
      patchy::Compositor{}.flatten_rgb8(patchy::psd::DocumentIo::read_file(adjustment_path));
  struct Probe {
    std::int32_t x;
    std::int32_t y;
    std::array<int, 3> expected;
  };
  for (const auto& probe : {Probe{8, 8, {255, 220, 220}}, Probe{16, 16, {187, 154, 154}},
                            Probe{24, 24, {247, 243, 243}}, Probe{8, 24, {255, 224, 224}}}) {
    const auto* px = flattened.pixel(probe.x, probe.y);
    for (std::size_t channel = 0; channel < 3; ++channel) {
      CHECK(std::abs(int{px[channel]} - probe.expected[channel]) <= 2);
    }
  }
}

// A 32-bit document stores its descriptor colors in linear light, like its float
// channels: Photoshop renders 300dpi.psb's solid fill of (172, 11, 11) as (215, 60, 60),
// the sRGB encoding Patchy applies to the pixel channels. Read as encoded bytes the fill
// came out a dark (172, 11, 11) (Testy, October 2026).
void psd_tools_32_bit_fill_color_is_linear_if_available() {
  const auto path = patchy::test::source_root_path() / "local-test-fixtures" / "psd-tools" / "tests" / "psd_files" /
                    "300dpi.psb";
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] psd-tools collection missing: " << path.string() << '\n';
    return;
  }
  const auto document = patchy::psd::DocumentIo::read_file(path);
  CHECK(document.metadata().values.at("psd.depth") == "32");
  const auto* shape = patchy::test::find_layer_named(std::as_const(document).layers(), "Shape 2");
  CHECK(shape != nullptr && shape->vector_shape() != nullptr);
  if (shape != nullptr && shape->vector_shape() != nullptr) {
    const auto color = shape->vector_shape()->fill.color;
    CHECK(std::abs(color.red - 214) <= 1 && std::abs(color.green - 59) <= 1 && std::abs(color.blue - 59) <= 1);
  }
  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* px = flattened.pixel(30, 90);
  CHECK(std::abs(px[0] - 215) <= 2 && std::abs(px[1] - 60) <= 2 && std::abs(px[2] - 60) <= 2);
}

// A noise gradient fill layer must stay a noise gradient through a save: writing it as a
// stop gradient left an empty color list and Photoshop dropped the fill (Testy, October
// 2026: "GRADIENTFILL became NORMAL" on psd-tools' noise-gradient-*.psd). The descriptor
// is regenerated for a pathless fill layer, so this compares what it says, plus the
// form marker and the absence of stop lists in the written bytes.
void psd_tools_noise_gradient_fill_survives_resave_if_available() {
  const auto path = patchy::test::source_root_path() / "local-test-fixtures" / "psd-tools" / "tests" / "psd_files" /
                    "gradients" / "noise-gradient-rgb.psd";
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] psd-tools collection missing: " << path.string() << '\n';
    return;
  }
  const auto document = patchy::psd::DocumentIo::read_file(path);
  CHECK(document.layers().size() == 4);
  const auto resaved = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto reread = patchy::psd::DocumentIo::read(resaved);
  CHECK(reread.layers().size() == 4);
  const auto contains = [](const std::vector<std::uint8_t>& bytes, std::string_view text) {
    return std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
  };
  for (std::size_t index = 1; index < 4 && index < reread.layers().size(); ++index) {
    const auto* before = std::as_const(document).layers()[index].vector_shape();
    const auto* after = std::as_const(reread).layers()[index].vector_shape();
    CHECK(before != nullptr && after != nullptr);
    if (before == nullptr || after == nullptr) {
      continue;
    }
    CHECK(before->fill.gradient.form == patchy::GradientDefinitionForm::Noise);
    CHECK(after->fill.gradient.form == patchy::GradientDefinitionForm::Noise);
    CHECK(after->fill.gradient.noise.seed == before->fill.gradient.noise.seed);
    CHECK(after->fill.gradient.noise.roughness == before->fill.gradient.noise.roughness);
    CHECK(after->fill.gradient.noise.color_model == before->fill.gradient.noise.color_model);
    CHECK(after->fill.gradient.noise.minimum == before->fill.gradient.noise.minimum);
    CHECK(after->fill.gradient.noise.maximum == before->fill.gradient.noise.maximum);
    CHECK(after->fill.gradient_noise_pre_seed == before->fill.gradient_noise_pre_seed);
    const auto block = psd_layer_block_payload(psd_layer_extra_data(resaved, static_cast<std::int16_t>(index)), "GdFl");
    CHECK(block.has_value());
    if (block.has_value()) {
      CHECK(contains(*block, "ClNs"));
      CHECK(!contains(*block, "CstS"));
      CHECK(!contains(*block, "Clrs"));
    }
  }
  // Photoshop stores the channel ranges as doubles (79.9988 for 80); rgb-noise, the
  // first layer above the background, is restricted to 80 / 40 / 20.
  CHECK(std::as_const(document).layers()[1].name() == "rgb-noise");
  if (const auto* shape = std::as_const(document).layers()[1].vector_shape(); shape != nullptr) {
    CHECK(shape->fill.gradient.noise.minimum[0] == 80);
    CHECK(shape->fill.gradient.noise.minimum[1] == 40);
    CHECK(shape->fill.gradient.noise.minimum[2] == 20);
  }
}

// Photoshop stores a gradient's interpolation method as a four-character code ("Lnr ",
// "Perc") as well as the long stringID. adjustment_clipping.psd holds a full-canvas
// "Lnr " gradient with the pixels Photoshop rendered for it, so redrawing the layer
// checks both the parse and Patchy's own linear-light ramp against Photoshop: read as
// Classic it was up to 60/255 off down the whole column.
void psd_tools_linear_gradient_fill_redraw_matches_photoshop_if_available() {
  const auto path = patchy::test::source_root_path() / "local-test-fixtures" / "psd-tools" / "tests" / "psd_files" /
                    "adjustments" / "adjustment_clipping.psd";
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] psd-tools collection missing: " << path.string() << '\n';
    return;
  }
  auto document = patchy::psd::DocumentIo::read_file(path);
  patchy::Layer* fill = nullptr;
  const auto find = [&](auto&& self, std::vector<patchy::Layer>& layers) -> void {
    for (auto& layer : layers) {
      if (layer.name() == "Gradient Fill 1") {
        fill = &layer;
      }
      if (!layer.children().empty()) {
        self(self, layer.children());
      }
    }
  };
  find(find, document.layers());
  CHECK(fill != nullptr);
  if (fill == nullptr) {
    return;
  }
  const auto* shape = std::as_const(*fill).vector_shape();
  CHECK(shape != nullptr);
  if (shape == nullptr) {
    return;
  }
  CHECK(shape->fill.gradient.interpolation == patchy::GradientInterpolationMethod::Linear);
  const auto stored = std::as_const(*fill).pixels();
  CHECK(stored.width() == 200 && stored.height() == 200);
  patchy::update_vector_shape_raster(*fill, patchy::Rect::from_size(document.width(), document.height()),
                                     &document.metadata().patterns);
  const auto& redrawn = std::as_const(*fill).pixels();
  CHECK(redrawn.width() == 200 && redrawn.height() == 200);
  if (redrawn.width() != 200 || redrawn.height() != 200 || stored.width() != 200 || stored.height() != 200) {
    return;
  }
  // The first row is skipped: Photoshop starts the ramp exactly on the first stop there.
  int worst = 0;
  for (std::int32_t y = 1; y < 200; ++y) {
    const auto* a = stored.pixel(100, y);
    const auto* b = redrawn.pixel(100, y);
    for (int channel = 0; channel < 3; ++channel) {
      worst = std::max(worst, std::abs(int{a[channel]} - int{b[channel]}));
    }
  }
  // 13/255 at worst today, in the steep first tenth of the ramp (a half-pixel phase
  // difference there); the bound leaves room for that and nothing like the old 60.
  if (worst > 16) {
    std::cout << "[INFO] linear gradient redraw is " << worst << "/255 off Photoshop at worst\n";
  }
  CHECK(worst <= 16);
}

// Photoshop names an unnamed layer "Layer N" by its position among the ordinary layers;
// its own manifest for these two files is the reference.
void psd_tools_unnamed_layers_take_photoshop_names_if_available() {
  const auto root = patchy::test::source_root_path() / "local-test-fixtures" / "psd-tools" / "tests" / "psd_files";
  const auto swatches = root / "descriptors" / "stroke-color-descriptors-hsb-with-rgb-mode.psd";
  const auto nested = root / "issues" / "issue397.psd";
  if (!std::filesystem::exists(swatches) || !std::filesystem::exists(nested)) {
    std::cout << "[SKIP] psd-tools collection missing: " << swatches.string() << '\n';
    return;
  }
  const auto document = patchy::psd::DocumentIo::read_file(swatches);
  CHECK(document.layers().size() == 8);
  for (std::size_t index = 0; index < document.layers().size(); ++index) {
    CHECK(document.layers()[index].name() == "Layer " + std::to_string(index + 1));
  }
  const auto nested_document = patchy::psd::DocumentIo::read_file(nested);
  CHECK(nested_document.layers().size() == 2);
  if (nested_document.layers().size() == 2) {
    const auto& group = nested_document.layers()[1];
    CHECK(group.children().size() == 1);
    if (group.children().size() == 1) {
      CHECK(group.children()[0].name() == "Layer 2");
    }
  }
}

// Builds a one-layer RGB PSD whose blue layer channel is RLE-compressed, with the
// caller's raw PackBits bytes substituted for the middle row. Real legacy files carry
// blocks of corrupt scanlines (a 2017 Dink map PSD has 58 of them in one channel) and
// Photoshop opens them, so the reader must recover rather than refuse the document.
std::vector<std::uint8_t> layered_psd_with_blue_row_bytes(
    std::span<const std::uint8_t> damaged_middle_row,
    std::optional<std::span<const std::uint8_t>> real_user_mask_payload = std::nullopt) {
  constexpr std::int32_t kWidth = 4;
  constexpr std::int32_t kHeight = 3;
  // Red and green rise across the layer; blue counts 100, 101, 102 per row so a
  // damaged row is obvious in the decoded pixels.
  const auto plane_value = [](int channel, std::int32_t x, std::int32_t y) {
    return static_cast<std::uint8_t>(channel == 0   ? 10 + x
                                     : channel == 1 ? 40 + y
                                                    : 100 + y);
  };

  patchy::psd::BigEndianWriter layer_extra;
  layer_extra.write_u32(0);  // no mask
  layer_extra.write_u32(0);  // no blending ranges
  write_pascal_padded(layer_extra, "Damaged", 4);

  std::array<std::vector<std::uint8_t>, 3> encoded_channels;
  for (int channel = 0; channel < 3; ++channel) {
    patchy::psd::BigEndianWriter channel_writer;
    std::vector<std::vector<std::uint8_t>> rows;
    for (std::int32_t y = 0; y < kHeight; ++y) {
      std::vector<std::uint8_t> row;
      for (std::int32_t x = 0; x < kWidth; ++x) {
        row.push_back(plane_value(channel, x, y));
      }
      auto encoded = patchy::psd::encode_packbits_row(row);
      if (channel == 2 && y == 1) {
        encoded.assign(damaged_middle_row.begin(), damaged_middle_row.end());
      }
      rows.push_back(std::move(encoded));
    }
    channel_writer.write_u16(1);  // RLE
    for (const auto& row : rows) {
      channel_writer.write_u16(static_cast<std::uint16_t>(row.size()));
    }
    for (const auto& row : rows) {
      channel_writer.write_bytes(row);
    }
    encoded_channels[static_cast<std::size_t>(channel)] = channel_writer.bytes();
  }

  patchy::psd::BigEndianWriter layer_info;
  layer_info.write_u16(1);  // one layer
  layer_info.write_u32(0);
  layer_info.write_u32(0);
  layer_info.write_u32(static_cast<std::uint32_t>(kHeight));
  layer_info.write_u32(static_cast<std::uint32_t>(kWidth));
  layer_info.write_u16(real_user_mask_payload.has_value() ? 4 : 3);
  for (std::uint16_t channel = 0; channel < 3; ++channel) {
    layer_info.write_u16(channel);
    layer_info.write_u32(static_cast<std::uint32_t>(encoded_channels[channel].size()));
  }
  if (real_user_mask_payload.has_value()) {
    layer_info.write_u16(0xFFFDU);  // real user mask (-3)
    layer_info.write_u32(static_cast<std::uint32_t>(2U + real_user_mask_payload->size()));
  }
  write_ascii4(layer_info, "8BIM");
  write_ascii4(layer_info, "norm");
  layer_info.write_u8(255);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u32(static_cast<std::uint32_t>(layer_extra.bytes().size()));
  layer_info.write_bytes(layer_extra.bytes());
  for (const auto& channel : encoded_channels) {
    layer_info.write_bytes(channel);
  }
  if (real_user_mask_payload.has_value()) {
    layer_info.write_u16(0);  // raw compression
    layer_info.write_bytes(*real_user_mask_payload);
  }
  if ((layer_info.bytes().size() % 2U) != 0) {
    layer_info.write_u8(0);
  }

  patchy::psd::BigEndianWriter layer_mask;
  layer_mask.write_u32(static_cast<std::uint32_t>(layer_info.bytes().size()));
  layer_mask.write_bytes(layer_info.bytes());
  layer_mask.write_u32(0);  // no global layer mask info

  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 3, static_cast<std::uint32_t>(kHeight),
                                                        static_cast<std::uint32_t>(kWidth), 8, 3});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(layer_mask.bytes().size()));
  writer.write_bytes(layer_mask.bytes());
  writer.write_u16(0);  // raw composite
  for (std::size_t i = 0; i < 3U * static_cast<std::size_t>(kWidth * kHeight); ++i) {
    writer.write_u8(0);
  }
  return writer.bytes();
}

// Reads the synthetic file and returns the layer's blue plane plus the notices.
std::pair<std::vector<std::uint8_t>, std::vector<std::string>> read_damaged_blue_plane(
    std::span<const std::uint8_t> damaged_middle_row) {
  const auto bytes = layered_psd_with_blue_row_bytes(damaged_middle_row);
  std::vector<std::string> notices;
  patchy::psd::ReadOptions options;
  options.notices = &notices;
  const auto document = patchy::psd::DocumentIo::read(bytes, options);
  CHECK(document.layers().size() == 1);
  const auto& pixels = document.layers().front().pixels();
  CHECK(pixels.width() == 4);
  CHECK(pixels.height() == 3);
  std::vector<std::uint8_t> blue;
  for (std::int32_t y = 0; y < 3; ++y) {
    for (std::int32_t x = 0; x < 4; ++x) {
      blue.push_back(pixels.pixel(x, y)[2]);
    }
  }
  return {std::move(blue), std::move(notices)};
}

bool has_damaged_row_notice(const std::vector<std::string>& notices) {
  return std::any_of(notices.begin(), notices.end(), [](const std::string& notice) {
    return notice.find("damaged") != std::string::npos;
  });
}

void psd_empty_real_user_mask_channel_does_not_truncate_layer() {
  const std::vector<std::uint8_t> good{0x03U, 50U, 51U, 52U, 53U};
  const std::span<const std::uint8_t> empty_payload;
  const auto bytes = layered_psd_with_blue_row_bytes(good, empty_payload);
  const auto document = patchy::psd::DocumentIo::read(bytes);
  CHECK(document.layers().size() == 1);
  const auto& pixels = document.layers().front().pixels();
  CHECK(pixels.width() == 4);
  CHECK(pixels.height() == 3);
  CHECK(pixels.pixel(0, 1)[2] == 50);
  CHECK(pixels.pixel(3, 1)[2] == 53);
}

void psd_real_user_mask_payload_is_skipped_without_losing_channel_alignment() {
  const std::vector<std::uint8_t> good{0x03U, 50U, 51U, 52U, 53U};
  const std::vector<std::uint8_t> undersized_real_mask{17U};
  const auto bytes =
      layered_psd_with_blue_row_bytes(good, std::span<const std::uint8_t>{undersized_real_mask});
  const auto document = patchy::psd::DocumentIo::read(bytes);
  CHECK(document.layers().size() == 1);
  const auto& pixels = document.layers().front().pixels();
  CHECK(pixels.pixel(0, 1)[2] == 50);
  CHECK(pixels.pixel(3, 1)[2] == 53);
}

// A run that overshoots the scanline is clipped at the row width; the rest of the
// channel still decodes from its own row-count entry, so the good rows stay exact.
void psd_overlong_packbits_row_recovers_and_notes() {
  // Repeat run of six 200s in a four-wide row, then a literal the row has no space for.
  const std::vector<std::uint8_t> damaged{0xFBU, 200U, 0x01U, 7U, 8U};
  const auto [blue, notices] = read_damaged_blue_plane(damaged);
  const std::vector<std::uint8_t> expected{100, 100, 100, 100, 200, 200, 200, 200, 102, 102, 102, 102};
  CHECK(blue == expected);
  CHECK(has_damaged_row_notice(notices));
}

// A scanline whose data ends early keeps zeroes for the rest instead of failing.
void psd_short_packbits_row_zero_fills_and_notes() {
  const std::vector<std::uint8_t> damaged{0x01U, 7U, 8U};  // two of the four bytes
  const auto [blue, notices] = read_damaged_blue_plane(damaged);
  const std::vector<std::uint8_t> expected{100, 100, 100, 100, 7, 8, 0, 0, 102, 102, 102, 102};
  CHECK(blue == expected);
  CHECK(has_damaged_row_notice(notices));
}

// A literal run whose bytes are cut off by the end of the row payload.
void psd_truncated_packbits_literal_recovers_and_notes() {
  const std::vector<std::uint8_t> damaged{0x03U, 7U, 8U};  // claims four bytes, supplies two
  const auto [blue, notices] = read_damaged_blue_plane(damaged);
  const std::vector<std::uint8_t> expected{100, 100, 100, 100, 7, 8, 0, 0, 102, 102, 102, 102};
  CHECK(blue == expected);
  CHECK(has_damaged_row_notice(notices));
}

// The lenient path must stay byte-identical to the strict decoder for valid rows, and
// must not invent a notice for a file that decodes cleanly.
void psd_valid_packbits_rows_decode_without_a_damage_notice() {
  const std::vector<std::uint8_t> good{0x03U, 50U, 51U, 52U, 53U};
  const auto [blue, notices] = read_damaged_blue_plane(good);
  const std::vector<std::uint8_t> expected{100, 100, 100, 100, 50, 51, 52, 53, 102, 102, 102, 102};
  CHECK(blue == expected);
  CHECK(!has_damaged_row_notice(notices));
}

void psd_packbits_scanline_decoder_clips_pads_and_reports() {
  const std::vector<std::uint8_t> exact{0xFEU, 9U, 0x00U, 4U};  // three 9s then one 4
  bool damaged = true;
  CHECK(patchy::psd::decode_packbits_scanline(exact, 4, &damaged) ==
        std::vector<std::uint8_t>({9, 9, 9, 4}));
  CHECK(!damaged);

  // A no-op header byte (-128) is skipped, exactly like the strict decoder.
  const std::vector<std::uint8_t> with_noop{0x80U, 0xFEU, 9U, 0x80U, 0x00U, 4U};
  damaged = true;
  CHECK(patchy::psd::decode_packbits_scanline(with_noop, 4, &damaged) ==
        std::vector<std::uint8_t>({9, 9, 9, 4}));
  CHECK(!damaged);

  const std::vector<std::uint8_t> overlong{0xF9U, 3U};  // repeat eight 3s into four bytes
  damaged = false;
  CHECK(patchy::psd::decode_packbits_scanline(overlong, 4, &damaged) ==
        std::vector<std::uint8_t>({3, 3, 3, 3}));
  CHECK(damaged);

  const std::vector<std::uint8_t> empty;
  damaged = false;
  CHECK(patchy::psd::decode_packbits_scanline(empty, 4, &damaged) ==
        std::vector<std::uint8_t>({0, 0, 0, 0}));
  CHECK(damaged);

  damaged = true;
  CHECK(patchy::psd::decode_packbits_scanline(empty, 0, &damaged).empty());
  CHECK(!damaged);
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
  // The UI forces the Import Notes popup from this value (GitHub issue 52).
  CHECK(read.metadata().values.at("psd.depth") == "16");
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
  CHECK(read.metadata().values.at("psd.depth") == "32");
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
  // Baseline July 2026: mean 0.50, far-off 0.9% (after the legacy 0xFFFF vmsk
  // combine-op fix let the CS4-era 'ic *' shape layers rasterize). The residual
  // sits in the icons' layer-effect shading and anti-aliased edges.
  CHECK(mean_abs_diff < 1.0);
  CHECK(far_off_fraction < 0.015);
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

void psd_app_icon_legacy_fixture_loads_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("APP_Icon_1024x1024.psd");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local APP icon fixture missing: " << path.string() << '\n';
    return;
  }

  const auto document = patchy::psd::DocumentIo::read_file(path);
  CHECK(document.width() == 1024);
  CHECK(document.height() == 1024);
  CHECK(!document.layers().empty());
}


// The bath-controls PSD (September 2026): "Group 1" holds four CS6-era
// stroke-only shape layers (vmsk + vstk with fillEnabled false + vscg, no fill
// block) that used to import vector-locked, refusing Free Transform on every
// folder or selection holding one. They import as editable stroke-only shapes.
void psd_stroke_only_shape_layers_fixture_loads_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("bath-controls-stroke-only-shapes.psd");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local bath-controls fixture missing: " << path.string() << '\n';
    return;
  }

  const auto document = patchy::psd::DocumentIo::read_file(path);
  const patchy::Layer* group = nullptr;
  for (const auto& layer : document.layers()) {
    if (layer.kind() == patchy::LayerKind::Group && layer.name() == "Group 1") {
      group = &layer;
    }
  }
  CHECK(group != nullptr);
  if (group == nullptr) {
    return;
  }
  int stroke_only_shapes = 0;
  for (const auto& child : group->children()) {
    if (child.name().rfind("Rounded Rectangle", 0) != 0) {
      continue;
    }
    const auto* content = child.vector_shape();
    if (content == nullptr) {
      // "copy 4" and "copy 5" were rasterized in Photoshop.
      continue;
    }
    ++stroke_only_shapes;
    CHECK(patchy::vector_lock_reason(child).empty());
    CHECK(content->fill.kind == patchy::VectorFillKind::None);
    CHECK(content->stroke.enabled && !content->stroke.fill_enabled);
    CHECK(std::fabs(content->stroke.width - 2.4193548387096775) < 0.01);
    CHECK(content->stroke.content.kind == patchy::VectorFillKind::Solid);
    CHECK(content->stroke.content.color.red == 255 && content->stroke.content.color.green == 0 &&
          content->stroke.content.color.blue == 0);
    CHECK(content->path.subpaths.size() == 1);
    // Photoshop's rendered stroke pixels stay authoritative until an edit.
    CHECK(!child.pixels().empty());
  }
  CHECK(stroke_only_shapes == 4);
}

}  // namespace


void psd_empty_document_saves_transparent_without_mutating_layers() {
  const patchy::Document document(9, 7, patchy::PixelFormat::rgba8());
  for (const bool large : {false, true}) {
    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document, {large});
    const auto restored = patchy::psd::DocumentIo::read(bytes);
    CHECK(restored.width() == 9 && restored.height() == 7);
    CHECK(document.layers().empty());
    std::vector<std::uint8_t> alpha;
    (void)patchy::Compositor{}.flatten_rgb8(restored, &alpha);
    CHECK(alpha.size() == 63);
    CHECK(std::all_of(alpha.begin(), alpha.end(), [](auto a) { return a == 0; }));
  }
}

void psd_writer_rejects_layer_record_count_overflow() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  for (int i = 0; i < 4000; ++i) {
    document.add_layer(patchy::Layer(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group));
  }
  for (const bool large : {false, true}) {
    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document, {large});
    CHECK(patchy::psd::DocumentIo::read(bytes).layers().size() == 4000);
  }
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "One too many", patchy::PixelBuffer()));
  for (const bool large : {false, true}) {
    bool rejected = false;
    try { (void)patchy::psd::DocumentIo::write_layered_rgb8(document, {large}); }
    catch (const std::runtime_error& e) { rejected = std::string(e.what()).find("8000") != std::string::npos; }
    CHECK(rejected);
  }
}


void psd_damaged_channel_cannot_consume_later_channels() {
  const std::vector<std::uint8_t> row{0x03U, 50U, 51U, 52U, 53U};
  auto bytes = layered_psd_with_blue_row_bytes(row);
  const std::string name = "Damaged";
  const auto at = std::search(bytes.begin(), bytes.end(), name.begin(), name.end());
  CHECK(at != bytes.end());
  const auto channel_start = static_cast<std::size_t>(at - bytes.begin()) + name.size();
  CHECK(bytes[channel_start] == 0 && bytes[channel_start+1] == 1);
  // First red scanline claims more bytes than its entire channel owns.
  bytes[channel_start+2] = 0xff;
  bytes[channel_start+3] = 0xff;
  std::vector<std::string> notices;
  patchy::psd::ReadOptions options;
  options.notices = &notices;
  const auto document = patchy::psd::DocumentIo::read(bytes, options);
  CHECK(document.layers().size() == 1);
  const auto& layer = document.layers().front();
  CHECK(layer.name() == "Damaged");
  const auto& pixels = layer.pixels();
  CHECK(pixels.pixel(0,1)[1] == 41);
  CHECK(pixels.pixel(0,1)[2] == 50 && pixels.pixel(3,1)[2] == 53);
  CHECK(has_damaged_row_notice(notices));
}

// ---- Grayscale-mode PSD import (issue 39) ----


bool has_notice_containing(const std::vector<std::string>& notices, std::string_view fragment) {
  return std::any_of(notices.begin(), notices.end(), [fragment](const std::string& notice) {
    return notice.find(fragment) != std::string::npos;
  });
}

void psd_flat_raw_gray8_imports_as_rgb() {
  const auto read = patchy::psd::DocumentIo::read(flat_psd_with_test_planes(false, 1, 2, 1, {{0, 200}}));
  CHECK(read.format() == patchy::PixelFormat::rgb8());
  CHECK(read.layers().size() == 1);
  CHECK(read.metadata().values.at("psd.color_mode") == "Grayscale");
  CHECK(read.metadata().values.at("psd.depth") == "8");
  const auto* px0 = read.layers().front().pixels().pixel(0, 0);
  const auto* px1 = read.layers().front().pixels().pixel(1, 0);
  CHECK(px0[0] == 0 && px0[1] == 0 && px0[2] == 0);
  CHECK(px1[0] == 200 && px1[1] == 200 && px1[2] == 200);
}

void psd_flat_rle_gray8_imports_as_rgb() {
  const auto read =
      patchy::psd::DocumentIo::read(flat_psd_with_test_planes(false, 1, 2, 1, {{37, 255}}, {}, 1));
  CHECK(read.layers().size() == 1);
  const auto* px0 = read.layers().front().pixels().pixel(0, 0);
  const auto* px1 = read.layers().front().pixels().pixel(1, 0);
  CHECK(px0[0] == 37 && px0[1] == 37 && px0[2] == 37);
  CHECK(px1[0] == 255 && px1[1] == 255 && px1[2] == 255);
}

void psd_flat_gray16_imports_as_rgb() {
  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 1, 1, 2, 16, 1});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u16(0);
  writer.write_u16(0x8080);
  writer.write_u16(0xFFFF);

  const auto read = patchy::psd::DocumentIo::read(writer.bytes());
  CHECK(read.format() == patchy::PixelFormat::rgb8());
  CHECK(read.layers().size() == 1);
  const auto* px0 = read.layers().front().pixels().pixel(0, 0);
  const auto* px1 = read.layers().front().pixels().pixel(1, 0);
  CHECK(px0[0] == 128 && px0[1] == 128 && px0[2] == 128);
  CHECK(px1[0] == 255 && px1[1] == 255 && px1[2] == 255);
}

std::vector<std::uint8_t> layered_gray_psd_with_transparency() {
  patchy::psd::BigEndianWriter layer_extra;
  layer_extra.write_u32(0);
  layer_extra.write_u32(0);
  write_pascal_padded(layer_extra, "Gray Layer", 4);

  patchy::psd::BigEndianWriter layer_info;
  layer_info.write_u16(1);
  layer_info.write_u32(0);
  layer_info.write_u32(0);
  layer_info.write_u32(1);
  layer_info.write_u32(2);
  layer_info.write_u16(2);
  for (const auto channel_id : {0xFFFFU, 0U}) {
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

  const std::array<std::array<std::uint8_t, 2>, 2> channels{{
      {255, 64},  // transparency
      {0, 200},   // gray
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
  patchy::psd::write_header(writer, patchy::psd::Header{false, 1, 1, 2, 8, 1});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(layer_mask.bytes().size()));
  writer.write_bytes(layer_mask.bytes());
  writer.write_u16(0);
  writer.write_u8(0);
  writer.write_u8(0);
  return writer.bytes();
}

void psd_layered_gray8_imports_as_rgba() {
  const auto read = patchy::psd::DocumentIo::read(layered_gray_psd_with_transparency());
  CHECK(read.layers().size() == 1);
  const auto& layer = read.layers().front();
  CHECK(layer.name() == "Gray Layer");
  CHECK(layer.pixels().format() == patchy::PixelFormat::rgba8());
  CHECK(read.metadata().values.at("psd.color_mode") == "Grayscale");
  const auto* px0 = layer.pixels().pixel(0, 0);
  const auto* px1 = layer.pixels().pixel(1, 0);
  CHECK(px0[0] == 0 && px0[1] == 0 && px0[2] == 0 && px0[3] == 255);
  CHECK(px1[0] == 200 && px1[1] == 200 && px1[2] == 200 && px1[3] == 64);
}

void psd_gray_extra_plane_imports_as_saved_channel() {
  patchy::psd::BigEndianWriter resources;
  write_test_image_resource(resources, 1006, "", test_alpha_channel_names_payload({"Spot"}));
  write_test_image_resource(resources, 1053, "", test_alpha_identifiers_payload({77}));

  const std::vector<std::vector<std::uint8_t>> planes{
      {10, 250},  // gray
      {7, 201},   // saved channel (after the single gray component)
  };
  for (const std::uint16_t compression : {std::uint16_t{0}, std::uint16_t{1}}) {
    const auto bytes = flat_psd_with_test_planes(false, 1, 2, 1, planes, resources.bytes(), compression);
    const auto read = patchy::psd::DocumentIo::read(bytes);
    CHECK(read.metadata().values.at("psd.color_mode") == "Grayscale");
    CHECK(read.layers().size() == 1);
    CHECK(read.layers().front().pixels().pixel(1, 0)[0] == 250);
    CHECK(read.channels().size() == 1);
    CHECK(read.channels().front().name() == "Spot");
    CHECK(read.channels().front().photoshop_identifier() == std::optional<std::uint32_t>{77});
    CHECK(read.channels().front().pixels().pixel(0, 0)[0] == 7);
    CHECK(read.channels().front().pixels().pixel(1, 0)[0] == 201);
  }
}

void psd_gray_icc_profile_converts_pixels_and_is_not_exported() {
  patchy::psd::BigEndianWriter resources;
  write_test_image_resource(resources, 1039, "", patchy::test::test_linear_gray_icc_profile());
  const auto bytes = flat_psd_with_test_planes(false, 1, 3, 1, {{0, 128, 255}}, resources.bytes());

  std::vector<std::string> notices;
  patchy::psd::ReadOptions options;
  options.notices = &notices;
  auto document = patchy::psd::DocumentIo::read(bytes, options);
  CHECK(has_notice_containing(notices, "Patchy Test Linear Gray"));
  // The gray profile describes the source, not the converted RGB pixels: preserved as a
  // raw resource, never promoted into color_state() nor exported as an RGB profile.
  CHECK(document.color_state().embedded_icc_profile.empty());
  CHECK(test_image_resource_payload(document.metadata().raw_psd_image_resources, 1039).has_value());
  CHECK(document.layers().size() == 1);
  const auto& pixels = document.layers().front().pixels();
  CHECK(pixels.pixel(0, 0)[0] == 0 && pixels.pixel(0, 0)[1] == 0 && pixels.pixel(0, 0)[2] == 0);
  CHECK(pixels.pixel(2, 0)[0] == 255 && pixels.pixel(2, 0)[1] == 255 && pixels.pixel(2, 0)[2] == 255);
  // Linear 128/255 encodes to sRGB 188 (lcms's 8-bit rounding may land a step either way).
  const auto* mid = pixels.pixel(1, 0);
  CHECK(mid[0] >= 186 && mid[0] <= 190);
  CHECK(mid[0] == mid[1] && mid[1] == mid[2]);

  const auto exported = psd_raw_image_resources(patchy::psd::DocumentIo::write_flat_rgb8(document));
  CHECK(!test_image_resource_payload(exported, 1039).has_value());
}

void psd_gray_unusable_icc_profile_copies_gray_unchanged() {
  patchy::psd::BigEndianWriter resources;
  const std::vector<std::uint8_t> bogus_icc{1, 2, 3, 4};
  write_test_image_resource(resources, 1039, "", bogus_icc);
  const auto bytes = flat_psd_with_test_planes(false, 1, 1, 1, {{128}}, resources.bytes());

  std::vector<std::string> notices;
  patchy::psd::ReadOptions options;
  options.notices = &notices;
  const auto document = patchy::psd::DocumentIo::read(bytes, options);
  CHECK(has_notice_containing(notices, "could not be used"));
  const auto* pixel = document.layers().front().pixels().pixel(0, 0);
  CHECK(pixel[0] == 128 && pixel[1] == 128 && pixel[2] == 128);
}

void psd_gray_descriptor_and_engine_colors_use_black_percentage() {
  // Photoshop probe (September 2026): a Color Overlay with 'Grsc' 'Gry ' 30 rendered the
  // same 179 as a 30% GrayColor fill, and 30% gray text stored /Type 0 /Values [1.0 .7].
  patchy::psd::DescriptorObject gray_color;
  gray_color.class_id = "Grsc";
  patchy::psd::DescriptorValue gray;
  gray.type = patchy::psd::DescriptorValue::Type::Double;
  gray.double_value = 30.0;
  gray_color.values["Gry "] = gray;
  patchy::psd::DescriptorObject effect;
  patchy::psd::DescriptorValue color_value;
  color_value.type = patchy::psd::DescriptorValue::Type::Object;
  color_value.object_value = std::make_shared<patchy::psd::DescriptorObject>(gray_color);
  effect.values["Clr "] = color_value;

  const patchy::psd::CmykColorConverter neutral{};
  const auto descriptor_color =
      patchy::psd::descriptor_rgb_color(effect, "Clr ", neutral, patchy::RgbColor{1, 2, 3});
  CHECK(descriptor_color == (patchy::RgbColor{179, 179, 179}));

  const std::string engine = "/FillColor << /Type 0 /Values [ 1.0 .7 ] >>";
  const auto text_color = patchy::psd::extract_engine_data_fill_color(
      std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(engine.data()), engine.size()),
      neutral);
  CHECK(text_color.has_value());
  CHECK(*text_color == (patchy::RgbColor{179, 179, 179}));
}

// The issue 39 reporter's file: 8-bit grayscale, nine pixel layers, Dot Gain 20% embedded.
void psd_issue39_grayscale_fixture_imports_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("issue-39/grayscale.psd");
  if (!std::filesystem::exists(path)) {
    std::printf("[SKIP] psd_issue39_grayscale_fixture_imports_if_available (no local fixture)\n");
    return;
  }
  std::vector<std::string> notices;
  patchy::psd::ReadOptions options;
  options.notices = &notices;
  const auto document = patchy::psd::DocumentIo::read_file(path, options);
  CHECK(document.width() == 2371);
  CHECK(document.height() == 1973);
  CHECK(document.layers().size() == 9);
  CHECK(document.metadata().values.at("psd.color_mode") == "Grayscale");
  CHECK(has_notice_containing(notices, "Dot Gain 20%"));

  const patchy::Layer* background = nullptr;
  const patchy::Layer* layer_one = nullptr;
  for (const auto& layer : document.layers()) {
    if (layer.name() == "Background") {
      background = &layer;
    } else if (layer.name() == "Layer 1") {
      layer_one = &layer;
    }
  }
  CHECK(background != nullptr);
  CHECK(layer_one != nullptr);
  if (background == nullptr || layer_one == nullptr) {
    return;
  }
  CHECK(background->pixels().width() == 2371);
  CHECK(background->pixels().height() == 1973);
  const auto* paper = background->pixels().pixel(0, 0);
  CHECK(paper[0] == 255 && paper[1] == 255 && paper[2] == 255);
  const auto* ink = background->pixels().pixel(327, 1000);
  CHECK(ink[0] == 0 && ink[1] == 0 && ink[2] == 0);
  CHECK(layer_one->pixels().format() == patchy::PixelFormat::rgba8());
  CHECK(layer_one->pixels().width() == 17);
  CHECK(layer_one->pixels().height() == 7);
}

// A 256-step gray ramp saved by Photoshop with Dot Gain 20% embedded, pinned against
// Photoshop's own Convert to Profile (sRGB, relative colorimetric, BPC, no dither) of
// the same document. The residue is lcms2-vs-ACE curve interpolation, as for CMYK.
void psd_gray_ramp_dotgain20_matches_photoshop_srgb_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("issue-39/gray-ramp-dotgain20.psd");
  if (!std::filesystem::exists(path)) {
    std::printf("[SKIP] psd_gray_ramp_dotgain20_matches_photoshop_srgb_if_available (no local fixture)\n");
    return;
  }
  const auto document = patchy::psd::DocumentIo::read_file(path);
  CHECK(document.width() == 256);
  CHECK(document.layers().size() == 1);
  const auto& pixels = document.layers().front().pixels();
  struct Expected {
    int gray;
    int srgb;
  };
  constexpr std::array<Expected, 17> kPhotoshop{{{0, 0},     {16, 22},    {32, 46},    {48, 66},    {64, 85},
                                                 {80, 102},   {96, 119},   {112, 134},  {128, 149},  {144, 164},
                                                 {160, 178},  {176, 192},  {192, 205},  {208, 218},  {224, 231},
                                                 {240, 243},  {255, 255}}};
  for (const auto& expected : kPhotoshop) {
    const auto* pixel = pixels.pixel(expected.gray, 0);
    CHECK(std::abs(static_cast<int>(pixel[0]) - expected.srgb) <= 2);
    CHECK(pixel[0] == pixel[1] && pixel[1] == pixel[2]);
  }
}

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
      {"psd_flat_raw_gray8_imports_as_rgb", psd_flat_raw_gray8_imports_as_rgb},
      {"psd_flat_rle_gray8_imports_as_rgb", psd_flat_rle_gray8_imports_as_rgb},
      {"psd_flat_gray16_imports_as_rgb", psd_flat_gray16_imports_as_rgb},
      {"psd_layered_gray8_imports_as_rgba", psd_layered_gray8_imports_as_rgba},
      {"psd_gray_extra_plane_imports_as_saved_channel", psd_gray_extra_plane_imports_as_saved_channel},
      {"psd_gray_icc_profile_converts_pixels_and_is_not_exported",
       psd_gray_icc_profile_converts_pixels_and_is_not_exported},
      {"psd_gray_unusable_icc_profile_copies_gray_unchanged",
       psd_gray_unusable_icc_profile_copies_gray_unchanged},
      {"psd_gray_descriptor_and_engine_colors_use_black_percentage",
       psd_gray_descriptor_and_engine_colors_use_black_percentage},
      {"psd_issue39_grayscale_fixture_imports_if_available",
       psd_issue39_grayscale_fixture_imports_if_available},
      {"psd_gray_ramp_dotgain20_matches_photoshop_srgb_if_available",
       psd_gray_ramp_dotgain20_matches_photoshop_srgb_if_available},
      {"psd_image_resources_round_trip_and_icc_profile_is_exposed",
       psd_image_resources_round_trip_and_icc_profile_is_exposed},
      {"psd_resolution_resource_units_are_display_only", psd_resolution_resource_units_are_display_only},
      {"psd_grid_guides_resource_round_trip_and_replaces_duplicates",
       psd_grid_guides_resource_round_trip_and_replaces_duplicates},
      {"psd_layered_rgb8_round_trips_pixel_layers", psd_layered_rgb8_round_trips_pixel_layers},
      {"psd_zero_length_layer_channels_read_as_empty", psd_zero_length_layer_channels_read_as_empty},
      {"psd_interface_mock2_loads_if_available", psd_interface_mock2_loads_if_available},
      {"psd_other_color_modes_convert_to_rgb_on_read", psd_other_color_modes_convert_to_rgb_on_read},
      {"psd_tools_other_color_modes_open_if_available", psd_tools_other_color_modes_open_if_available},
      {"psd_tools_cmyk_levels_run_on_the_inks_if_available", psd_tools_cmyk_levels_run_on_the_inks_if_available},
      {"psd_tools_lab_color_descriptors_parse_if_available", psd_tools_lab_color_descriptors_parse_if_available},
      {"psd_tools_gradient_fill_without_angle_runs_left_to_right_if_available",
       psd_tools_gradient_fill_without_angle_runs_left_to_right_if_available},
      {"psd_tools_grayscale_adjustments_apply_to_the_gray_channel_if_available",
       psd_tools_grayscale_adjustments_apply_to_the_gray_channel_if_available},
      {"psd_tools_corpus_reads_and_round_trips_if_available", psd_tools_corpus_reads_and_round_trips_if_available},
      {"psd_tools_group_fill_matches_photoshop_if_available", psd_tools_group_fill_matches_photoshop_if_available},
      {"psd_tools_noise_gradient_fill_survives_resave_if_available",
       psd_tools_noise_gradient_fill_survives_resave_if_available},
      {"psd_tools_32_bit_fill_color_is_linear_if_available", psd_tools_32_bit_fill_color_is_linear_if_available},
      {"psd_tools_linear_gradient_fill_redraw_matches_photoshop_if_available",
       psd_tools_linear_gradient_fill_redraw_matches_photoshop_if_available},
      {"psd_tools_unnamed_layers_take_photoshop_names_if_available",
       psd_tools_unnamed_layers_take_photoshop_names_if_available},
      {"psd_empty_real_user_mask_channel_does_not_truncate_layer",
       psd_empty_real_user_mask_channel_does_not_truncate_layer},
      {"psd_real_user_mask_payload_is_skipped_without_losing_channel_alignment",
       psd_real_user_mask_payload_is_skipped_without_losing_channel_alignment},
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
      {"psd_app_icon_legacy_fixture_loads_if_available", psd_app_icon_legacy_fixture_loads_if_available},
      {"psd_stroke_only_shape_layers_fixture_loads_if_available",
       psd_stroke_only_shape_layers_fixture_loads_if_available},
      {"psd_layered_writer_uses_rle_for_compressible_layer_channels",
       psd_layered_writer_uses_rle_for_compressible_layer_channels},
      {"psd_opaque_rgb_layers_write_transparency_unless_background",
       psd_opaque_rgb_layers_write_transparency_unless_background},
      {"psd_layer_locks_import_and_export_lspf", psd_layer_locks_import_and_export_lspf},
      {"psd_layer_masks_render_and_round_trip", psd_layer_masks_render_and_round_trip},
      {"psd_group_layer_mask_round_trips", psd_group_layer_mask_round_trips},
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
      {"psd_overlong_packbits_row_recovers_and_notes", psd_overlong_packbits_row_recovers_and_notes},
      {"psd_short_packbits_row_zero_fills_and_notes", psd_short_packbits_row_zero_fills_and_notes},
      {"psd_truncated_packbits_literal_recovers_and_notes",
       psd_truncated_packbits_literal_recovers_and_notes},
      {"psd_valid_packbits_rows_decode_without_a_damage_notice",
       psd_valid_packbits_rows_decode_without_a_damage_notice},
      {"psd_packbits_scanline_decoder_clips_pads_and_reports",
       psd_packbits_scanline_decoder_clips_pads_and_reports},
      {"psd_empty_document_saves_transparent_without_mutating_layers", psd_empty_document_saves_transparent_without_mutating_layers},
      {"psd_writer_rejects_layer_record_count_overflow", psd_writer_rejects_layer_record_count_overflow},
      {"psd_damaged_channel_cannot_consume_later_channels", psd_damaged_channel_cannot_consume_later_channels},
  };
}
