#include "color/color_management.hpp"
#include "core/adjustment_layer.hpp"
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "filters/filter_registry.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_registry.hpp"
#include "formats/gif_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/ilbm_document_io.hpp"
#include "formats/palette_io.hpp"
#include "formats/pcx_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "plugins/plugin_host.hpp"
#include "psd/abr_reader.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_document_io.hpp"
#include "core/magnetic_lasso.hpp"
#include "core/palette.hpp"
#include "core/palette_presets.hpp"
#include "core/pixel_tools.hpp"
#include "core/quick_select.hpp"
#include "render/compositor.hpp"
#include "render/layer_compositor.hpp"
#include "render/tile_cache.hpp"
#include "support/string_utils.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <exception>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using patchy::test::TestCase;

patchy::PixelBuffer solid_rgb(std::int32_t width, std::int32_t height, std::uint8_t r, std::uint8_t g,
                                 std::uint8_t b) {
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = r;
      px[1] = g;
      px[2] = b;
    }
  }
  return pixels;
}

patchy::PixelBuffer solid_rgba(std::int32_t width, std::int32_t height, std::uint8_t r, std::uint8_t g,
                                  std::uint8_t b, std::uint8_t a) {
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = r;
      px[1] = g;
      px[2] = b;
      px[3] = a;
    }
  }
  return pixels;
}

patchy::Document make_tool_document() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 48, 255, 255, 255));
  document.add_pixel_layer("Paint", solid_rgba(64, 48, 0, 0, 0, 0));
  return document;
}

void layer_affine_transform_metadata_parses_serializes_and_composes() {
  const auto parsed = patchy::parse_layer_affine_transform("1 2 3 4 5 6");
  CHECK(parsed.has_value());
  CHECK((*parsed)[0] == 1.0);
  CHECK((*parsed)[5] == 6.0);
  CHECK(!patchy::parse_layer_affine_transform("1 2 3 4 5").has_value());

  const patchy::LayerAffineTransform translate{1.0, 0.0, 0.0, 1.0, 10.0, 20.0};
  const patchy::LayerAffineTransform scale{2.0, 0.0, 0.0, 3.0, 0.0, 0.0};
  const auto composed = patchy::compose_layer_affine_transform(translate, scale);
  CHECK(composed[0] == 2.0);
  CHECK(composed[3] == 3.0);
  CHECK(composed[4] == 10.0);
  CHECK(composed[5] == 20.0);

  const auto serialized = patchy::serialize_layer_affine_transform(composed);
  const auto reparsed = patchy::parse_layer_affine_transform(serialized);
  CHECK(reparsed.has_value());
  CHECK((*reparsed)[0] == composed[0]);
  CHECK((*reparsed)[3] == composed[3]);
  CHECK((*reparsed)[4] == composed[4]);
  CHECK((*reparsed)[5] == composed[5]);

  patchy::Layer layer(7, "Text", patchy::PixelBuffer(4, 4, patchy::PixelFormat::rgba8()));
  layer.metadata()[patchy::kLayerMetadataTextTransform] = serialized;
  layer.metadata()[patchy::kLayerMetadataPsdTextTransform] = "1 0 0 1 99 99";
  CHECK(patchy::parse_layer_affine_transform(layer.metadata().at(patchy::kLayerMetadataTextTransform)).has_value());
}

void layer_lock_flags_and_inheritance_work() {
  patchy::Layer folder(1, "Folder", patchy::LayerKind::Group);
  patchy::Layer child(2, "Child", solid_rgba(2, 2, 20, 40, 60, 255));
  const auto child_id = child.id();
  folder.add_child(std::move(child));

  std::vector<patchy::Layer> layers;
  layers.push_back(std::move(folder));
  CHECK(patchy::layer_lock_flags(layers.front()) == patchy::kLayerLockNone);
  CHECK(patchy::layer_effective_lock_flags(layers, child_id) == patchy::kLayerLockNone);
  CHECK(!patchy::layer_has_locked_ancestor(layers, child_id));

  patchy::set_layer_locks_position(layers.front(), true);
  CHECK(patchy::layer_locks_position(layers.front()));
  CHECK(patchy::layer_effectively_locks_position(layers, child_id));
  CHECK(!patchy::layer_effectively_locks_image_pixels(layers, child_id));
  CHECK(patchy::layer_has_locked_ancestor(layers, child_id));

  auto* child_layer = patchy::find_layer_in_tree(layers, child_id);
  CHECK(child_layer != nullptr);
  patchy::set_layer_locks_image_pixels(*child_layer, true);
  CHECK(patchy::layer_effective_lock_flags(layers, child_id) ==
        (patchy::kLayerLockImagePixels | patchy::kLayerLockPosition));

  patchy::set_layer_locks_position(layers.front(), false);
  CHECK(patchy::layer_effective_lock_flags(layers, child_id) == patchy::kLayerLockImagePixels);

  patchy::set_layer_locked(layers.front(), true);
  CHECK(patchy::layer_is_locked(layers.front()));
  CHECK(patchy::layer_is_effectively_locked(layers, child_id));
}

void layer_content_revision_ignores_translation_and_tracks_render_content() {
  patchy::Layer layer(7, "Paint", solid_rgba(4, 4, 20, 40, 60, 255));
  const auto initial_content_revision = layer.content_revision();
  const auto initial_render_revision = layer.render_revision();

  layer.set_bounds(patchy::Rect{10, 12, 4, 4});
  CHECK(layer.content_revision() == initial_content_revision);
  CHECK(layer.render_revision() > initial_render_revision);

  const auto after_move_content_revision = layer.content_revision();
  layer.set_name("Renamed Paint");
  CHECK(layer.content_revision() == after_move_content_revision);

  layer.set_opacity(0.5F);
  CHECK(layer.content_revision() > after_move_content_revision);

  const auto after_opacity_content_revision = layer.content_revision();
  layer.set_blend_mode(patchy::BlendMode::Multiply);
  CHECK(layer.content_revision() > after_opacity_content_revision);

  const auto after_blend_content_revision = layer.content_revision();
  auto* px = layer.pixels().pixel(0, 0);
  px[0] = 120;
  CHECK(layer.content_revision() > after_blend_content_revision);

  const auto after_pixels_content_revision = layer.content_revision();
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  layer.layer_style().strokes.push_back(stroke);
  CHECK(layer.content_revision() > after_pixels_content_revision);

  const auto after_style_content_revision = layer.content_revision();
  patchy::LayerMask mask;
  mask.bounds = patchy::Rect{10, 12, 4, 4};
  mask.pixels = patchy::PixelBuffer(4, 4, patchy::PixelFormat::gray8());
  mask.pixels.clear(255);
  layer.set_mask(std::move(mask));
  CHECK(layer.content_revision() > after_style_content_revision);
}

patchy::Document make_filter_document() {
  patchy::Document document(32, 24, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(32, 24, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x * 8);
      px[1] = static_cast<std::uint8_t>(y * 10);
      px[2] = static_cast<std::uint8_t>(80 + (x + y) % 120);
    }
  }
  document.add_pixel_layer("Filter Source", std::move(pixels));
  return document;
}

patchy::EditOptions tool_options(std::uint8_t r = 220, std::uint8_t g = 20, std::uint8_t b = 40) {
  patchy::EditOptions options;
  options.primary = patchy::EditColor{r, g, b, 255};
  options.secondary = patchy::EditColor{255, 255, 255, 255};
  options.brush_size = 7;
  return options;
}

void write_u16_le(std::ofstream& file, std::uint16_t value) {
  file.put(static_cast<char>(value & 0xFFU));
  file.put(static_cast<char>((value >> 8U) & 0xFFU));
}

void write_u32_le(std::ofstream& file, std::uint32_t value) {
  file.put(static_cast<char>(value & 0xFFU));
  file.put(static_cast<char>((value >> 8U) & 0xFFU));
  file.put(static_cast<char>((value >> 16U) & 0xFFU));
  file.put(static_cast<char>((value >> 24U) & 0xFFU));
}

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

void write_ascii4(patchy::psd::BigEndianWriter& writer, const char (&value)[5]) {
  for (int i = 0; i < 4; ++i) {
    writer.write_u8(static_cast<std::uint8_t>(value[i]));
  }
}

void write_pascal_padded(patchy::psd::BigEndianWriter& writer, const std::string& value,
                         std::size_t padded_multiple) {
  const auto length = std::min<std::size_t>(value.size(), 255);
  writer.write_u8(static_cast<std::uint8_t>(length));
  writer.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(value.data()), length));
  const auto consumed = 1 + length;
  const auto padded = ((consumed + padded_multiple - 1) / padded_multiple) * padded_multiple;
  for (std::size_t i = consumed; i < padded; ++i) {
    writer.write_u8(0);
  }
}

std::string read_pascal_padded(patchy::psd::BigEndianReader& reader, std::size_t padded_multiple) {
  const auto start = reader.position();
  const auto length = reader.read_u8();
  auto bytes = reader.read_bytes(length);
  const auto consumed = reader.position() - start;
  const auto padded = ((consumed + padded_multiple - 1) / padded_multiple) * padded_multiple;
  if (padded > consumed) {
    reader.skip(padded - consumed);
  }
  return std::string(bytes.begin(), bytes.end());
}

std::vector<std::string> psd_raw_layer_record_names(std::span<const std::uint8_t> bytes) {
  patchy::psd::BigEndianReader reader(bytes);
  (void)patchy::psd::read_header(reader);

  const auto color_mode_length = reader.read_u32();
  reader.skip(color_mode_length);
  const auto image_resource_length = reader.read_u32();
  reader.skip(image_resource_length);

  const auto layer_mask_length = reader.read_u32();
  CHECK(layer_mask_length > 0);
  const auto layer_info_length = reader.read_u32();
  CHECK(layer_info_length > 0);

  const auto layer_count_raw = static_cast<std::int16_t>(reader.read_u16());
  const auto layer_count = layer_count_raw < 0 ? -layer_count_raw : layer_count_raw;
  std::vector<std::string> names;
  names.reserve(static_cast<std::size_t>(layer_count));

  for (std::int16_t index = 0; index < layer_count; ++index) {
    reader.skip(16);  // bounds
    const auto channel_count = reader.read_u16();
    for (std::uint16_t channel = 0; channel < channel_count; ++channel) {
      reader.skip(2);  // channel id
      reader.skip(4);  // channel byte length
    }
    reader.skip(12);  // blend signature/key, opacity, clipping, flags, filler

    const auto extra_length = reader.read_u32();
    const auto extra_end = reader.position() + extra_length;
    const auto mask_length = reader.read_u32();
    reader.skip(mask_length);
    const auto blending_ranges_length = reader.read_u32();
    reader.skip(blending_ranges_length);
    names.push_back(read_pascal_padded(reader, 4));
    if (reader.position() < extra_end) {
      reader.skip(extra_end - reader.position());
    }
  }

  return names;
}

struct PsdLayerChannelRecord {
  std::int16_t id{0};
  std::uint32_t length{0};
  std::uint16_t compression{0};
};

std::vector<PsdLayerChannelRecord> psd_layer_channel_records(std::span<const std::uint8_t> bytes) {
  patchy::psd::BigEndianReader reader(bytes);
  (void)patchy::psd::read_header(reader);

  const auto color_mode_length = reader.read_u32();
  reader.skip(color_mode_length);
  const auto image_resource_length = reader.read_u32();
  reader.skip(image_resource_length);

  const auto layer_mask_length = reader.read_u32();
  if (layer_mask_length == 0) {
    return {};
  }
  const auto layer_info_length = reader.read_u32();
  if (layer_info_length == 0) {
    return {};
  }

  const auto layer_count_raw = static_cast<std::int16_t>(reader.read_u16());
  const auto layer_count = layer_count_raw < 0 ? -layer_count_raw : layer_count_raw;
  std::vector<PsdLayerChannelRecord> records;
  for (std::int16_t index = 0; index < layer_count; ++index) {
    reader.skip(16);  // bounds
    const auto channel_count = reader.read_u16();
    for (std::uint16_t channel = 0; channel < channel_count; ++channel) {
      const auto id = static_cast<std::int16_t>(reader.read_u16());
      const auto length = reader.read_u32();
      records.push_back(PsdLayerChannelRecord{id, length, 0});
    }
    reader.skip(12);  // blend signature/key, opacity, clipping, flags, filler

    const auto extra_length = reader.read_u32();
    reader.skip(extra_length);
  }

  for (auto& record : records) {
    CHECK(record.length >= 2U);
    record.compression = reader.read_u16();
    reader.skip(record.length - 2U);
  }

  return records;
}

std::uint16_t psd_composite_compression(std::span<const std::uint8_t> bytes) {
  patchy::psd::BigEndianReader reader(bytes);
  (void)patchy::psd::read_header(reader);

  const auto color_mode_length = reader.read_u32();
  reader.skip(color_mode_length);
  const auto image_resource_length = reader.read_u32();
  reader.skip(image_resource_length);
  const auto layer_mask_length = reader.read_u32();
  reader.skip(layer_mask_length);
  return reader.read_u16();
}

std::uint32_t read_u32_be_at(std::span<const std::uint8_t> bytes, std::size_t offset) {
  CHECK(offset + 4U <= bytes.size());
  return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
         static_cast<std::uint32_t>(bytes[offset + 3U]);
}

std::int32_t read_i32_be_at(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::int32_t>(read_u32_be_at(bytes, offset));
}

std::uint64_t read_u64_be_at(std::span<const std::uint8_t> bytes, std::size_t offset) {
  CHECK(offset + 8U <= bytes.size());
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < 8U; ++index) {
    value = (value << 8U) | bytes[offset + index];
  }
  return value;
}

double read_f64_be_at(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return std::bit_cast<double>(read_u64_be_at(bytes, offset));
}

std::array<double, 4> parse_bounds_metadata4(const std::string& text) {
  std::istringstream stream(text);
  std::array<double, 4> values{};
  for (auto& value : values) {
    stream >> value;
  }
  CHECK(static_cast<bool>(stream));
  return values;
}

std::vector<std::uint8_t> utf16be_test_bytes(std::string_view text) {
  std::vector<std::uint8_t> bytes;
  for (const auto ch : text) {
    bytes.push_back(0);
    bytes.push_back(static_cast<std::uint8_t>(ch));
  }
  return bytes;
}

std::int32_t test_path_fixed_value(std::int32_t value, std::int32_t extent) {
  return static_cast<std::int32_t>(
      std::llround((static_cast<double>(value) * 16777216.0) / static_cast<double>(extent)));
}

std::vector<std::uint8_t> single_knot_vector_mask_payload(std::int32_t document_x, std::int32_t document_y,
                                                          std::int32_t document_width,
                                                          std::int32_t document_height) {
  patchy::psd::BigEndianWriter writer;
  writer.write_u32(3);
  writer.write_u32(0);
  writer.write_u16(1);

  const auto fixed_x = test_path_fixed_value(document_x, document_width);
  const auto fixed_y = test_path_fixed_value(document_y, document_height);
  for (int index = 0; index < 3; ++index) {
    writer.write_u32(static_cast<std::uint32_t>(fixed_y));
    writer.write_u32(static_cast<std::uint32_t>(fixed_x));
  }
  return writer.bytes();
}

void moved_layer_metadata_translates_linked_masks_and_vector_paths() {
  patchy::Layer layer(51, "Masked", solid_rgba(10, 10, 30, 40, 50, 255));
  layer.set_bounds(patchy::Rect{30, 40, 10, 10});
  patchy::LayerMask mask;
  mask.bounds = patchy::Rect{30, 40, 10, 10};
  mask.pixels = patchy::PixelBuffer(10, 10, patchy::PixelFormat::gray8());
  mask.pixels.clear(255);
  layer.set_mask(std::move(mask));
  layer.unknown_psd_blocks().push_back(
      patchy::UnknownPsdBlock{"vmsk", single_knot_vector_mask_payload(30, 40, 100, 200)});
  layer.metadata()[patchy::kLayerMetadataText] = "Text";
  layer.metadata()[patchy::kLayerMetadataTextTransform] = "1 0 0 1 30 40";

  patchy::translate_moved_layer_metadata(layer, 5, -10, 100, 200);

  CHECK(layer.mask().has_value());
  CHECK(layer.mask()->bounds.x == 35);
  CHECK(layer.mask()->bounds.y == 30);
  const auto& vector_mask = layer.unknown_psd_blocks().front().payload;
  const auto expected_x = test_path_fixed_value(30, 100) + test_path_fixed_value(5, 100);
  const auto expected_y = test_path_fixed_value(40, 200) + test_path_fixed_value(-10, 200);
  CHECK(read_i32_be_at(vector_mask, 10U) == expected_y);
  CHECK(read_i32_be_at(vector_mask, 14U) == expected_x);
  CHECK(read_i32_be_at(vector_mask, 18U) == expected_y);
  CHECK(read_i32_be_at(vector_mask, 22U) == expected_x);
  CHECK(read_i32_be_at(vector_mask, 26U) == expected_y);
  CHECK(read_i32_be_at(vector_mask, 30U) == expected_x);

  const auto transform = patchy::parse_layer_affine_transform(
      layer.metadata().at(patchy::kLayerMetadataTextTransform));
  CHECK(transform.has_value());
  CHECK((*transform)[4] == 35.0);
  CHECK((*transform)[5] == 30.0);
}

void moved_layer_metadata_leaves_unlinked_masks_stationary() {
  patchy::Layer layer(52, "Masked", solid_rgba(10, 10, 30, 40, 50, 255));
  patchy::LayerMask mask;
  mask.bounds = patchy::Rect{30, 40, 10, 10};
  mask.pixels = patchy::PixelBuffer(10, 10, patchy::PixelFormat::gray8());
  mask.pixels.clear(255);
  layer.set_mask(std::move(mask));
  auto vector_mask = single_knot_vector_mask_payload(30, 40, 100, 200);
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"vmsk", vector_mask});
  patchy::set_layer_mask_linked(layer, false);

  patchy::translate_moved_layer_metadata(layer, 5, -10, 100, 200);

  CHECK(layer.mask().has_value());
  CHECK(layer.mask()->bounds.x == 30);
  CHECK(layer.mask()->bounds.y == 40);
  CHECK(layer.unknown_psd_blocks().front().payload == vector_mask);
}

std::vector<std::uint8_t> psd_layer_extra_data(std::span<const std::uint8_t> bytes, std::int16_t target_index) {
  patchy::psd::BigEndianReader reader(bytes);
  (void)patchy::psd::read_header(reader);

  const auto color_mode_length = reader.read_u32();
  reader.skip(color_mode_length);
  const auto image_resource_length = reader.read_u32();
  reader.skip(image_resource_length);

  const auto layer_mask_length = reader.read_u32();
  CHECK(layer_mask_length > 0);
  const auto layer_info_length = reader.read_u32();
  CHECK(layer_info_length > 0);
  const auto layer_count_raw = static_cast<std::int16_t>(reader.read_u16());
  const auto layer_count = layer_count_raw < 0 ? -layer_count_raw : layer_count_raw;
  CHECK(layer_count > 0);
  CHECK(target_index >= 0);
  CHECK(target_index < layer_count);

  for (std::int16_t index = 0; index < layer_count; ++index) {
    reader.skip(16);  // bounds
    const auto channel_count = reader.read_u16();
    for (std::uint16_t channel = 0; channel < channel_count; ++channel) {
      reader.skip(2);  // channel id
      reader.skip(4);  // channel byte length
    }
    reader.skip(12);  // blend signature/key, opacity, clipping, flags, filler

    const auto extra_length = reader.read_u32();
    auto extra_data = reader.read_bytes(extra_length);
    if (index == target_index) {
      return extra_data;
    }
  }

  CHECK(false);
  return {};
}

std::vector<std::uint8_t> psd_first_layer_extra_data(std::span<const std::uint8_t> bytes) {
  return psd_layer_extra_data(bytes, 0);
}

std::optional<std::vector<std::uint8_t>> psd_layer_block_payload(std::span<const std::uint8_t> extra_data,
                                                                 const char (&target_key)[5]) {
  patchy::psd::BigEndianReader reader(extra_data);
  const auto mask_length = reader.read_u32();
  reader.skip(mask_length);
  const auto blending_ranges_length = reader.read_u32();
  reader.skip(blending_ranges_length);
  (void)read_pascal_padded(reader, 4);

  while (reader.remaining() >= 12U) {
    const auto signature = reader.read_bytes(4);
    if (signature != std::vector<std::uint8_t>{'8', 'B', 'I', 'M'} &&
        signature != std::vector<std::uint8_t>{'8', 'B', '6', '4'}) {
      break;
    }
    const auto key = reader.read_bytes(4);
    const auto payload_length = reader.read_u32();
    auto payload = reader.read_bytes(payload_length);
    if (std::equal(key.begin(), key.end(), target_key)) {
      return payload;
    }
  }
  return std::nullopt;
}

int replace_all_ascii_same_length(std::vector<std::uint8_t>& bytes, std::string_view needle,
                                  std::string_view replacement) {
  CHECK(needle.size() == replacement.size());
  int replacements = 0;
  auto search_begin = bytes.begin();
  while (true) {
    const auto found = std::search(search_begin, bytes.end(), needle.begin(), needle.end());
    if (found == bytes.end()) {
      break;
    }
    std::copy(replacement.begin(), replacement.end(), found);
    search_begin = found + static_cast<std::ptrdiff_t>(replacement.size());
    ++replacements;
  }
  return replacements;
}

patchy::LevelsRecord psd_levels_payload_record(std::span<const std::uint8_t> payload, int record_index) {
  patchy::psd::BigEndianReader reader(payload);
  CHECK(reader.read_u16() == 2);
  CHECK(record_index >= 0);
  CHECK(record_index < 29);
  reader.skip(static_cast<std::size_t>(record_index) * 10U);
  const auto black_input = static_cast<int>(reader.read_u16());
  const auto white_input = static_cast<int>(reader.read_u16());
  const auto black_output = static_cast<int>(reader.read_u16());
  const auto white_output = static_cast<int>(reader.read_u16());
  const auto gamma_percent = static_cast<int>(reader.read_u16());
  return patchy::LevelsRecord{black_input, white_input, gamma_percent, black_output, white_output};
}

std::vector<std::uint8_t> test_photoshop_levels_payload(const std::array<patchy::LevelsRecord, 4>& records) {
  patchy::psd::BigEndianWriter writer;
  writer.write_u16(2);
  for (int index = 0; index < 29; ++index) {
    const auto record = index < 4 ? records[static_cast<std::size_t>(index)] : patchy::LevelsRecord{};
    writer.write_u16(static_cast<std::uint16_t>(record.black_input));
    writer.write_u16(static_cast<std::uint16_t>(record.white_input));
    writer.write_u16(static_cast<std::uint16_t>(record.black_output));
    writer.write_u16(static_cast<std::uint16_t>(record.white_output));
    writer.write_u16(static_cast<std::uint16_t>(record.gamma_percent));
  }
  return writer.bytes();
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

void write_test_layer_block(patchy::psd::BigEndianWriter& writer, const char (&key)[5],
                            std::span<const std::uint8_t> payload) {
  write_ascii4(writer, "8BIM");
  write_ascii4(writer, key);
  writer.write_u32(static_cast<std::uint32_t>(payload.size()));
  writer.write_bytes(payload);
  if ((payload.size() % 2U) != 0) {
    writer.write_u8(0);
  }
}

std::vector<std::uint8_t> single_adjustment_layer_psd(
    const std::vector<std::pair<std::array<char, 4>, std::vector<std::uint8_t>>>& blocks) {
  patchy::psd::BigEndianWriter layer_extra;
  layer_extra.write_u32(0);
  layer_extra.write_u32(0);
  write_pascal_padded(layer_extra, "Levels", 4);
  for (const auto& [key, payload] : blocks) {
    char key_string[5] = {key[0], key[1], key[2], key[3], '\0'};
    write_test_layer_block(layer_extra, key_string, payload);
  }

  patchy::psd::BigEndianWriter layer_info;
  layer_info.write_u16(1);
  layer_info.write_u32(0);
  layer_info.write_u32(0);
  layer_info.write_u32(1);
  layer_info.write_u32(1);
  layer_info.write_u16(0);
  write_ascii4(layer_info, "8BIM");
  write_ascii4(layer_info, "norm");
  layer_info.write_u8(255);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u32(static_cast<std::uint32_t>(layer_extra.bytes().size()));
  layer_info.write_bytes(layer_extra.bytes());
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
  writer.write_u8(0);
  writer.write_u8(200);
  writer.write_u8(0);
  return writer.bytes();
}

std::string engine_utf16be_literal(std::string_view text) {
  std::string literal;
  literal.push_back('(');
  literal.push_back(static_cast<char>(0xFE));
  literal.push_back(static_cast<char>(0xFF));
  for (const auto ch : text) {
    literal.push_back('\0');
    literal.push_back(ch);
  }
  literal.push_back(')');
  return literal;
}

std::vector<std::uint8_t> single_text_layer_psd(std::span<const std::uint8_t> text_payload) {
  patchy::psd::BigEndianWriter layer_extra;
  layer_extra.write_u32(0);
  layer_extra.write_u32(0);
  write_pascal_padded(layer_extra, "Text Layer", 4);
  write_test_layer_block(layer_extra, "TySh", text_payload);

  patchy::psd::BigEndianWriter layer_info;
  layer_info.write_u16(1);
  layer_info.write_u32(12);
  layer_info.write_u32(10);
  layer_info.write_u32(82);
  layer_info.write_u32(210);
  layer_info.write_u16(0);
  write_ascii4(layer_info, "8BIM");
  write_ascii4(layer_info, "norm");
  layer_info.write_u8(255);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u32(static_cast<std::uint32_t>(layer_extra.bytes().size()));
  layer_info.write_bytes(layer_extra.bytes());
  if ((layer_info.bytes().size() % 2U) != 0) {
    layer_info.write_u8(0);
  }

  patchy::psd::BigEndianWriter layer_mask;
  layer_mask.write_u32(static_cast<std::uint32_t>(layer_info.bytes().size()));
  layer_mask.write_bytes(layer_info.bytes());
  layer_mask.write_u32(0);

  constexpr std::uint32_t width = 240;
  constexpr std::uint32_t height = 120;
  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 3, height, width, 8, 3});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(layer_mask.bytes().size()));
  writer.write_bytes(layer_mask.bytes());
  writer.write_u16(0);
  for (std::size_t i = 0; i < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U; ++i) {
    writer.write_u8(255);
  }
  return writer.bytes();
}

std::vector<std::uint8_t> section_divider_payload(std::uint32_t type, const char (&blend_mode)[5]) {
  patchy::psd::BigEndianWriter payload;
  payload.write_u32(type);
  write_ascii4(payload, "8BIM");
  write_ascii4(payload, blend_mode);
  return payload.bytes();
}

std::vector<std::uint8_t> section_divider_payload(std::uint32_t type) {
  patchy::psd::BigEndianWriter payload;
  payload.write_u32(type);
  return payload.bytes();
}

std::vector<std::uint8_t> psd_raw_image_resources(std::span<const std::uint8_t> bytes) {
  patchy::psd::BigEndianReader reader(bytes);
  (void)patchy::psd::read_header(reader);

  const auto color_mode_length = reader.read_u32();
  reader.skip(color_mode_length);
  const auto image_resource_length = reader.read_u32();
  return reader.read_bytes(image_resource_length);
}

std::optional<std::vector<std::uint8_t>> test_image_resource_payload(std::span<const std::uint8_t> resources,
                                                                     std::uint16_t id) {
  patchy::psd::BigEndianReader reader(resources);
  while (reader.remaining() > 0) {
    auto signature = reader.read_bytes(4);
    CHECK(signature[0] == '8');
    CHECK(signature[1] == 'B');
    const auto resource_id = reader.read_u16();
    (void)read_pascal_padded(reader, 2);
    const auto payload_length = reader.read_u32();
    auto payload = reader.read_bytes(payload_length);
    if ((payload_length % 2U) != 0 && reader.remaining() > 0) {
      reader.skip(1);
    }
    if (resource_id == id) {
      return payload;
    }
  }
  return std::nullopt;
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

void write_rgb8_bmp_artifact(const std::string& name, const patchy::PixelBuffer& pixels) {
  CHECK(pixels.format().bit_depth == patchy::BitDepth::UInt8);
  CHECK(pixels.format().channels >= 3);
  const auto out_dir = std::filesystem::path("test-artifacts");
  std::filesystem::create_directories(out_dir);
  const auto path = out_dir / (name + ".bmp");
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not write test image artifact");
  }

  const auto row_stride = static_cast<std::uint32_t>(((pixels.width() * 3 + 3) / 4) * 4);
  const auto pixel_bytes = row_stride * static_cast<std::uint32_t>(pixels.height());
  const auto file_size = 14U + 40U + pixel_bytes;

  file.put('B');
  file.put('M');
  write_u32_le(file, file_size);
  write_u16_le(file, 0);
  write_u16_le(file, 0);
  write_u32_le(file, 14U + 40U);

  write_u32_le(file, 40U);
  write_u32_le(file, static_cast<std::uint32_t>(pixels.width()));
  write_u32_le(file, static_cast<std::uint32_t>(pixels.height()));
  write_u16_le(file, 1);
  write_u16_le(file, 24);
  write_u32_le(file, 0);
  write_u32_le(file, pixel_bytes);
  write_u32_le(file, 2835);
  write_u32_le(file, 2835);
  write_u32_le(file, 0);
  write_u32_le(file, 0);

  std::vector<std::uint8_t> padding(row_stride - static_cast<std::uint32_t>(pixels.width() * 3), 0);
  for (std::int32_t y = pixels.height() - 1; y >= 0; --y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* px = pixels.pixel(x, y);
      file.put(static_cast<char>(px[2]));
      file.put(static_cast<char>(px[1]));
      file.put(static_cast<char>(px[0]));
    }
    file.write(reinterpret_cast<const char*>(padding.data()), static_cast<std::streamsize>(padding.size()));
  }
}

void write_bmp_artifact(const std::string& name, const patchy::Document& document) {
  write_rgb8_bmp_artifact(name, patchy::Compositor{}.flatten_rgb8(document));
}

std::filesystem::path qual_rca_pinout_fixture_path() {
  return patchy::test::committed_psd_fixture_path("qual_rca_pinout.psd");
}

std::filesystem::path arrows_fixture_path() {
  return patchy::test::committed_psd_fixture_path("arrows.psd");
}

const patchy::Layer* find_layer_named(const std::vector<patchy::Layer>& layers, const std::string& name) {
  for (const auto& layer : layers) {
    if (layer.name() == name) {
      return &layer;
    }
    if (const auto* found = find_layer_named(layer.children(), name); found != nullptr) {
      return found;
    }
  }
  return nullptr;
}

const patchy::LayerDropShadow* first_enabled_drop_shadow(const patchy::Layer& layer) {
  const auto& shadows = layer.layer_style().drop_shadows;
  const auto found = std::find_if(shadows.begin(), shadows.end(), [](const patchy::LayerDropShadow& shadow) {
    return shadow.enabled;
  });
  return found == shadows.end() ? nullptr : &*found;
}

const patchy::LayerInnerShadow* first_enabled_inner_shadow(const patchy::Layer& layer) {
  const auto& shadows = layer.layer_style().inner_shadows;
  const auto found = std::find_if(shadows.begin(), shadows.end(), [](const patchy::LayerInnerShadow& shadow) {
    return shadow.enabled;
  });
  return found == shadows.end() ? nullptr : &*found;
}

const patchy::LayerInnerGlow* first_enabled_inner_glow(const patchy::Layer& layer) {
  const auto& glows = layer.layer_style().inner_glows;
  const auto found = std::find_if(glows.begin(), glows.end(), [](const patchy::LayerInnerGlow& glow) {
    return glow.enabled;
  });
  return found == glows.end() ? nullptr : &*found;
}

bool layer_has_psd_block(const patchy::Layer& layer, const std::string& key) {
  return std::any_of(layer.unknown_psd_blocks().begin(), layer.unknown_psd_blocks().end(),
                     [&key](const patchy::UnknownPsdBlock& block) { return block.key == key; });
}

bool close_float(float actual, float expected, float tolerance = 0.01F) {
  return std::abs(actual - expected) <= tolerance;
}

struct RgbDiffMetrics {
  std::uint64_t pixels{0};
  std::uint64_t differing_pixels{0};
  double mean_abs_channel_delta{0.0};
  int max_channel_delta{0};
};

RgbDiffMetrics rgb_diff_metrics(const patchy::PixelBuffer& left, const patchy::PixelBuffer& right) {
  CHECK(left.width() == right.width());
  CHECK(left.height() == right.height());
  CHECK(left.format().channels >= 3);
  CHECK(right.format().channels >= 3);

  RgbDiffMetrics metrics;
  metrics.pixels = static_cast<std::uint64_t>(left.width()) * static_cast<std::uint64_t>(left.height());
  std::uint64_t total_delta = 0;
  for (std::int32_t y = 0; y < left.height(); ++y) {
    for (std::int32_t x = 0; x < left.width(); ++x) {
      const auto* a = left.pixel(x, y);
      const auto* b = right.pixel(x, y);
      int pixel_delta = 0;
      for (int channel = 0; channel < 3; ++channel) {
        const auto delta = std::abs(static_cast<int>(a[channel]) - static_cast<int>(b[channel]));
        total_delta += static_cast<std::uint64_t>(delta);
        pixel_delta += delta;
        metrics.max_channel_delta = std::max(metrics.max_channel_delta, delta);
      }
      if (pixel_delta > 0) {
        ++metrics.differing_pixels;
      }
    }
  }
  if (metrics.pixels > 0) {
    metrics.mean_abs_channel_delta =
        static_cast<double>(total_delta) / static_cast<double>(metrics.pixels * 3ULL);
  }
  return metrics;
}

patchy::PixelBuffer rgb_diff_image(const patchy::PixelBuffer& left, const patchy::PixelBuffer& right) {
  CHECK(left.width() == right.width());
  CHECK(left.height() == right.height());
  patchy::PixelBuffer diff(left.width(), left.height(), patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < left.height(); ++y) {
    for (std::int32_t x = 0; x < left.width(); ++x) {
      const auto* a = left.pixel(x, y);
      const auto* b = right.pixel(x, y);
      auto* out = diff.pixel(x, y);
      for (int channel = 0; channel < 3; ++channel) {
        out[channel] =
            static_cast<std::uint8_t>(std::min(255, std::abs(static_cast<int>(a[channel]) -
                                                             static_cast<int>(b[channel])) * 4));
      }
    }
  }
  return diff;
}

void write_qual_rca_pinout_report(const RgbDiffMetrics& metrics, const patchy::Document& editable_document) {
  std::filesystem::create_directories("test-artifacts");
  std::vector<std::string> recommended_features;
  recommended_features.push_back("Improve Photoshop text rasterization/font metric parity for editable text layers.");
  recommended_features.push_back("Decode additional Photoshop layer effects advertised by lfx2: inner shadow, inner glow, satin, pattern overlay.");
  recommended_features.push_back("Classify preserved PSD metadata blocks such as shmd and fxrp so reports can name unsupported data precisely.");

  int styled_layers = 0;
  int text_layers = 0;
  for (const auto& layer : editable_document.layers()) {
    if (!layer.layer_style().empty()) {
      ++styled_layers;
    }
    if (patchy::layer_is_text(layer)) {
      ++text_layers;
    }
  }

  {
    std::ofstream report(std::filesystem::path("test-artifacts") / "psd_qual_rca_pinout_compatibility_report.txt");
    report << "PSD compatibility comparison: qual_rca_pinout.psd\n";
    report << "Reference: embedded Photoshop composite\n";
    report << "Patchy render: editable layer composite\n";
    report << "Pixels: " << metrics.pixels << "\n";
    report << "Differing pixels: " << metrics.differing_pixels << "\n";
    report << "Mean absolute channel delta: " << std::fixed << std::setprecision(3)
           << metrics.mean_abs_channel_delta << "\n";
    report << "Max channel delta: " << metrics.max_channel_delta << "\n";
    report << "Parsed styled layers: " << styled_layers << "\n";
    report << "Parsed editable text layers: " << text_layers << "\n";
    report << "Recommendations:\n";
    for (const auto& recommendation : recommended_features) {
      report << "- " << recommendation << "\n";
    }
  }

  {
    std::ofstream json(std::filesystem::path("test-artifacts") / "psd_qual_rca_pinout_compatibility_report.json");
    json << "{\n";
    json << "  \"fixture\": \"qual_rca_pinout.psd\",\n";
    json << "  \"reference\": \"embedded Photoshop composite\",\n";
    json << "  \"pixels\": " << metrics.pixels << ",\n";
    json << "  \"differing_pixels\": " << metrics.differing_pixels << ",\n";
    json << "  \"mean_abs_channel_delta\": " << std::fixed << std::setprecision(3)
         << metrics.mean_abs_channel_delta << ",\n";
    json << "  \"max_channel_delta\": " << metrics.max_channel_delta << ",\n";
    json << "  \"styled_layers\": " << styled_layers << ",\n";
    json << "  \"text_layers\": " << text_layers << ",\n";
    json << "  \"recommendations\": [\n";
    for (std::size_t i = 0; i < recommended_features.size(); ++i) {
      json << "    \"" << recommended_features[i] << "\"" << (i + 1U == recommended_features.size() ? "\n" : ",\n");
    }
    json << "  ]\n";
    json << "}\n";
  }
}

patchy::LayerId active_tool_layer(const patchy::Document& document) {
  return document.active_layer_id().value();
}

void pixel_buffer_tracks_shape_and_rows() {
  patchy::PixelBuffer pixels(4, 3, patchy::PixelFormat::rgba8());
  CHECK(pixels.width() == 4);
  CHECK(pixels.height() == 3);
  CHECK(pixels.byte_size() == 4U * 3U * 4U);
  CHECK(pixels.row(1).size() == 16U);
  pixels.pixel(2, 1)[0] = 77;
  CHECK(pixels.row(1)[8] == 77);
}

void pixel_buffer_copy_shares_storage_until_mutated() {
  auto original = solid_rgba(8, 8, 10, 20, 30, 255);
  const auto* original_ptr = original.data().data();

  patchy::PixelBuffer copy = original;
  // A const read must not detach: the copy still shares the original storage.
  CHECK(std::as_const(copy).data().data() == original_ptr);
  CHECK(std::as_const(original).data().data() == original_ptr);

  // Mutating the copy detaches it onto private storage and leaves the original intact.
  copy.pixel(0, 0)[0] = 200;
  CHECK(copy.data().data() != original_ptr);
  CHECK(std::as_const(original).data().data() == original_ptr);
  CHECK(std::as_const(original).pixel(0, 0)[0] == 10);
  CHECK(std::as_const(copy).pixel(0, 0)[0] == 200);

  // Once unique again, repeated non-const access reuses the same storage.
  const auto* detached_ptr = copy.data().data();
  copy.clear(0);
  CHECK(copy.data().data() == detached_ptr);
}

const std::uint8_t* shared_pixel_ptr(const patchy::Document& document, patchy::LayerId id) {
  const auto* layer = document.find_layer(id);
  return layer == nullptr ? nullptr : layer->pixels().data().data();
}

void document_snapshot_shares_pixels_when_only_moving_a_layer() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgba8());
  const auto layer_id = document.add_pixel_layer("Paint", solid_rgba(64, 48, 10, 20, 30, 255)).id();
  const auto* live_ptr = shared_pixel_ptr(document, layer_id);

  // Simulate an undo snapshot: copying the whole document must not duplicate pixel bytes.
  patchy::Document snapshot = document;
  CHECK(shared_pixel_ptr(snapshot, layer_id) == live_ptr);

  // Moving the live layer changes only its bounds, so the snapshot keeps sharing the pixels.
  document.find_layer(layer_id)->set_bounds(patchy::Rect{16, 16, 64, 48});
  CHECK(shared_pixel_ptr(document, layer_id) == live_ptr);
  CHECK(shared_pixel_ptr(snapshot, layer_id) == live_ptr);

  // Editing the live pixels detaches the live layer while the snapshot retains the originals.
  document.find_layer(layer_id)->pixels().pixel(0, 0)[0] = 99;
  CHECK(shared_pixel_ptr(document, layer_id) != live_ptr);
  CHECK(shared_pixel_ptr(snapshot, layer_id) == live_ptr);
  CHECK(snapshot.find_layer(layer_id)->pixels().pixel(0, 0)[0] == 10);
}

void document_adds_and_finds_layers() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer("Paint", solid_rgb(2, 2, 10, 20, 30));
  CHECK(layer.id() == 1);
  CHECK(document.active_layer_id().value() == layer.id());
  CHECK(document.find_layer(layer.id()) == &layer);
}

void document_removes_layers_and_updates_active_layer() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  auto first = document.add_pixel_layer("First", solid_rgb(2, 2, 10, 20, 30)).id();
  auto second = document.add_pixel_layer("Second", solid_rgb(2, 2, 40, 50, 60)).id();
  CHECK(document.active_layer_id().value() == second);
  CHECK(document.remove_layer(second));
  CHECK(document.find_layer(second) == nullptr);
  CHECK(document.active_layer_id().value() == first);
  CHECK(document.remove_layer(first));
  CHECK(!document.active_layer_id().has_value());
}

void document_can_clear_active_layer() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  const auto layer = document.add_pixel_layer("Paint", solid_rgb(2, 2, 10, 20, 30)).id();
  CHECK(document.active_layer_id().value() == layer);
  document.clear_active_layer();
  CHECK(!document.active_layer_id().has_value());
  document.set_active_layer(layer);
  CHECK(document.active_layer_id().value() == layer);
}

void default_non_group_layer_id_selects_topmost_visible_unlocked_pixel_child() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(2, 2, 10, 20, 30));
  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  const auto folder_id = folder.id();
  patchy::Layer child(document.allocate_layer_id(), "Child", solid_rgb(2, 2, 40, 50, 60));
  const auto child_id = child.id();
  folder.add_child(std::move(child));
  document.add_layer(std::move(folder));

  CHECK(document.active_layer_id().value() == folder_id);
  const auto default_layer_id = patchy::default_non_group_layer_id(document.layers());
  CHECK(default_layer_id.has_value());
  CHECK(*default_layer_id == child_id);
}

void default_non_group_layer_id_uses_visible_adjustment_before_hidden_pixels() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  patchy::Layer hidden_pixel(document.allocate_layer_id(), "Hidden Pixel", solid_rgb(2, 2, 10, 20, 30));
  hidden_pixel.set_visible(false);
  const auto hidden_pixel_id = hidden_pixel.id();
  document.add_layer(std::move(hidden_pixel));
  patchy::Layer adjustment(document.allocate_layer_id(), "Adjustment", patchy::LayerKind::Adjustment);
  const auto adjustment_id = adjustment.id();
  document.add_layer(std::move(adjustment));

  CHECK(document.active_layer_id().value() == adjustment_id);
  CHECK(patchy::default_non_group_layer_id(document.layers()).value() == adjustment_id);
  document.find_layer(adjustment_id)->set_visible(false);
  CHECK(patchy::default_non_group_layer_id(document.layers()).value() == hidden_pixel_id);
}

void default_non_group_layer_id_ignores_locked_content_and_folder_only_trees() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  patchy::Layer child(document.allocate_layer_id(), "Locked Child", solid_rgb(2, 2, 40, 50, 60));
  const auto child_id = child.id();
  folder.add_child(std::move(child));
  patchy::set_layer_locks_image_pixels(folder, true);
  document.add_layer(std::move(folder));
  patchy::Layer adjustment(document.allocate_layer_id(), "Adjustment", patchy::LayerKind::Adjustment);
  const auto adjustment_id = adjustment.id();
  document.add_layer(std::move(adjustment));

  CHECK(patchy::default_non_group_layer_id(document.layers()).value() == adjustment_id);
  document.find_layer(adjustment_id)->set_visible(false);
  CHECK(patchy::default_non_group_layer_id(document.layers()).value() == child_id);

  patchy::Document folder_only(2, 2, patchy::PixelFormat::rgb8());
  patchy::Layer empty_folder(folder_only.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  empty_folder.add_child(patchy::Layer(folder_only.allocate_layer_id(), "Nested Folder", patchy::LayerKind::Group));
  folder_only.add_layer(std::move(empty_folder));
  CHECK(!patchy::default_non_group_layer_id(folder_only.layers()).has_value());
}

void default_non_group_layer_id_allows_position_locked_pixels() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  auto& position_locked = document.add_pixel_layer("Position Locked", solid_rgb(2, 2, 40, 50, 60));
  const auto position_locked_id = position_locked.id();
  patchy::set_layer_locks_position(position_locked, true);
  patchy::Layer adjustment(document.allocate_layer_id(), "Adjustment", patchy::LayerKind::Adjustment);
  const auto adjustment_id = adjustment.id();
  document.add_layer(std::move(adjustment));

  CHECK(patchy::default_non_group_layer_id(document.layers()).value() == position_locked_id);

  patchy::set_layer_locks_image_pixels(*document.find_layer(position_locked_id), true);
  CHECK(patchy::default_non_group_layer_id(document.layers()).value() == adjustment_id);
}

void layer_drop_request_moves_multiple_layers_into_folder() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  const auto background_id = document.add_pixel_layer("Background", solid_rgb(2, 2, 10, 20, 30)).id();
  const auto paint_id = document.add_pixel_layer("Paint", solid_rgb(2, 2, 40, 50, 60)).id();
  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  const auto folder_id = folder.id();
  document.add_layer(std::move(folder));

  patchy::LayerDropRequest request{{paint_id, background_id}, folder_id, patchy::LayerDropPosition::OnItem};
  CHECK(patchy::move_layers_for_drop(document.layers(), request));
  CHECK(document.layers().size() == 1);

  const auto& moved_folder = document.layers().front();
  CHECK(moved_folder.id() == folder_id);
  CHECK(moved_folder.children().size() == 2);
  CHECK(moved_folder.children()[0].id() == background_id);
  CHECK(moved_folder.children()[1].id() == paint_id);
}

void layer_drop_roots_ignore_selected_descendants() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(2, 2, 10, 20, 30));
  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  const auto folder_id = folder.id();
  patchy::Layer child(document.allocate_layer_id(), "Child", solid_rgb(2, 2, 40, 50, 60));
  const auto child_id = child.id();
  folder.add_child(std::move(child));
  document.add_layer(std::move(folder));

  const auto roots = patchy::root_drop_layer_ids(document.layers(), {folder_id, child_id});
  CHECK(roots.size() == 1);
  CHECK(roots.front() == folder_id);
}

void document_print_settings_default_and_copy() {
  patchy::Document document(16, 12, patchy::PixelFormat::rgb8());
  CHECK(document.print_settings().horizontal_ppi == 300.0);
  CHECK(document.print_settings().vertical_ppi == 300.0);

  document.print_settings().horizontal_ppi = 144.0;
  document.print_settings().vertical_ppi = 150.0;
  const auto copied = document;
  CHECK(copied.print_settings().horizontal_ppi == 144.0);
  CHECK(copied.print_settings().vertical_ppi == 150.0);
}

void document_grid_guides_default_and_copy() {
  patchy::Document document(16, 12, patchy::PixelFormat::rgb8());
  CHECK(document.grid_settings().horizontal_cycle_32 == 576);
  CHECK(document.grid_settings().vertical_cycle_32 == 576);
  CHECK(document.guides().empty());

  document.grid_settings().horizontal_cycle_32 = 640;
  document.grid_settings().vertical_cycle_32 = 960;
  document.guides().push_back(patchy::DocumentGuide{patchy::GuideOrientation::Vertical, 321});
  document.guides().push_back(patchy::DocumentGuide{patchy::GuideOrientation::Horizontal, 654});

  const auto copied = document;
  CHECK(copied.grid_settings().horizontal_cycle_32 == 640);
  CHECK(copied.grid_settings().vertical_cycle_32 == 960);
  CHECK(copied.guides().size() == 2);
  CHECK(copied.guides()[0].orientation == patchy::GuideOrientation::Vertical);
  CHECK(copied.guides()[0].position_32 == 321);
  CHECK(copied.guides()[1].orientation == patchy::GuideOrientation::Horizontal);
  CHECK(copied.guides()[1].position_32 == 654);
}

void compositor_flattens_visible_layers() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(1, 1, 10, 20, 30));
  auto top_pixels = solid_rgb(1, 1, 110, 120, 130);
  auto& top = document.add_pixel_layer("Top", std::move(top_pixels));
  top.set_opacity(0.5F);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* px = flattened.pixel(0, 0);
  CHECK(px[0] == 60);
  CHECK(px[1] == 70);
  CHECK(px[2] == 80);
}

void compositor_multiply_uses_empty_backdrop_as_transparent() {
  patchy::Document transparent_document(1, 1, patchy::PixelFormat::rgba8());
  auto& transparent_multiply =
      transparent_document.add_pixel_layer("Multiply", solid_rgba(1, 1, 200, 100, 50, 128));
  transparent_multiply.set_blend_mode(patchy::BlendMode::Multiply);

  const auto transparent_flattened = patchy::Compositor{}.flatten_rgb8(transparent_document);
  const auto* transparent_px = transparent_flattened.pixel(0, 0);
  CHECK(transparent_px[0] == 200);
  CHECK(transparent_px[1] == 100);
  CHECK(transparent_px[2] == 50);

  patchy::Document opaque_document(1, 1, patchy::PixelFormat::rgb8());
  opaque_document.add_pixel_layer("Base", solid_rgb(1, 1, 100, 160, 240));
  auto& opaque_multiply = opaque_document.add_pixel_layer("Multiply", solid_rgba(1, 1, 200, 100, 50, 255));
  opaque_multiply.set_blend_mode(patchy::BlendMode::Multiply);

  const auto opaque_flattened = patchy::Compositor{}.flatten_rgb8(opaque_document);
  const auto* opaque_px = opaque_flattened.pixel(0, 0);
  CHECK(opaque_px[0] == 78);
  CHECK(opaque_px[1] == 62);
  CHECK(opaque_px[2] == 47);
}

void compositor_applies_extended_blend_modes() {
  struct ExpectedBlend {
    patchy::BlendMode mode;
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
  };

  // Saturation/Luminosity were re-pinned July 2026 when the non-separable modes moved from
  // an HSL-lightness approximation to the PDF-spec luma algorithm Photoshop and Aseprite
  // share (Hue/Color/Exclusion/LinearDodge/Subtract/Divide were added at the same time).
  const std::vector<ExpectedBlend> expected = {
      {patchy::BlendMode::Darken, 100, 60, 100},
      {patchy::BlendMode::Lighten, 200, 120, 140},
      {patchy::BlendMode::ColorDodge, 255, 156, 230},
      {patchy::BlendMode::ColorBurn, 58, 0, 0},
      {patchy::BlendMode::HardLight, 189, 56, 109},
      {patchy::BlendMode::SoftLight, 134, 86, 126},
      {patchy::BlendMode::Difference, 100, 60, 40},
      {patchy::BlendMode::LinearBurn, 45, 0, 0},
      {patchy::BlendMode::PinLight, 144, 120, 140},
      {patchy::BlendMode::Saturation, 60, 130, 200},
      {patchy::BlendMode::Luminosity, 90, 110, 130},
      {patchy::BlendMode::Exclusion, 144, 124, 130},
      {patchy::BlendMode::Hue, 143, 103, 114},
      {patchy::BlendMode::Color, 210, 70, 110},
      {patchy::BlendMode::LinearDodge, 255, 180, 240},
      {patchy::BlendMode::Subtract, 0, 60, 40},
      {patchy::BlendMode::Divide, 128, 255, 255},
  };

  for (const auto& blend : expected) {
    patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(1, 1, 100, 120, 140));
    auto& top = document.add_pixel_layer("Top", solid_rgba(1, 1, 200, 60, 100, 255));
    top.set_blend_mode(blend.mode);

    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    const auto* px = flattened.pixel(0, 0);
    CHECK(px[0] == blend.r);
    CHECK(px[1] == blend.g);
    CHECK(px[2] == blend.b);
  }
}

void compositor_renders_layer_style_drop_shadow_gradient_and_stroke() {
  patchy::Document document(12, 12, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(12, 12, 255, 255, 255));

  patchy::Layer styled_layer(document.allocate_layer_id(), "Styled", solid_rgba(4, 4, 220, 20, 20, 255));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(patchy::Rect{3, 3, 4, 4});

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Normal;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 180.0F;
  shadow.distance = 2.0F;
  shadow.size = 0.0F;
  layer.layer_style().drop_shadows.push_back(shadow);

  patchy::LayerGradientFill fill;
  fill.enabled = true;
  fill.blend_mode = patchy::BlendMode::Normal;
  fill.opacity = 1.0F;
  fill.gradient.angle_degrees = 0.0F;
  fill.gradient.color_stops.push_back(patchy::GradientColorStop{0.0F, patchy::RgbColor{20, 60, 240}});
  fill.gradient.color_stops.push_back(patchy::GradientColorStop{1.0F, patchy::RgbColor{20, 220, 80}});
  layer.layer_style().gradient_fills.push_back(fill);

  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = patchy::BlendMode::Normal;
  stroke.color = patchy::RgbColor{255, 220, 0};
  stroke.opacity = 1.0F;
  stroke.size = 1.0F;
  stroke.position = patchy::LayerStrokePosition::Outside;
  layer.layer_style().strokes.push_back(stroke);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* shadow_px = flattened.pixel(8, 4);
  CHECK(shadow_px[0] < 20);
  CHECK(shadow_px[1] < 20);
  CHECK(shadow_px[2] < 20);

  const auto* left_gradient = flattened.pixel(3, 4);
  const auto* right_gradient = flattened.pixel(6, 4);
  CHECK(left_gradient[2] > left_gradient[1]);
  CHECK(right_gradient[1] > right_gradient[2]);

  const auto* stroke_px = flattened.pixel(2, 4);
  CHECK(stroke_px[0] > 240);
  CHECK(stroke_px[1] > 200);
  CHECK(stroke_px[2] < 40);
}

void compositor_renders_layer_style_bevel_emboss() {
  patchy::Document document(12, 12, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(12, 12, 255, 255, 255));

  patchy::Layer styled_layer(document.allocate_layer_id(), "Bevel", solid_rgba(6, 6, 120, 120, 120, 255));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(patchy::Rect{3, 3, 6, 6});

  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.highlight_blend_mode = patchy::BlendMode::Normal;
  bevel.highlight_color = patchy::RgbColor{255, 255, 255};
  bevel.highlight_opacity = 1.0F;
  bevel.shadow_blend_mode = patchy::BlendMode::Normal;
  bevel.shadow_color = patchy::RgbColor{0, 0, 0};
  bevel.shadow_opacity = 1.0F;
  bevel.angle_degrees = 120.0F;
  bevel.altitude_degrees = 30.0F;
  bevel.depth = 3.0F;
  bevel.size = 2.0F;
  layer.layer_style().bevels.push_back(bevel);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* highlighted = flattened.pixel(3, 3);
  const auto* shadowed = flattened.pixel(8, 8);
  CHECK(highlighted[0] > 150);
  CHECK(shadowed[0] < 100);
}

void compositor_renders_bevel_across_thin_checkbox_edges() {
  patchy::Document document(40, 40, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(40, 40, 255, 255, 255));

  auto pixels = solid_rgba(32, 32, 255, 242, 0, 0);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (x < 5 || y < 5 || x >= pixels.width() - 5 || y >= pixels.height() - 5) {
        auto* px = pixels.pixel(x, y);
        px[0] = 255;
        px[1] = 242;
        px[2] = 0;
        px[3] = 255;
      }
    }
  }

  patchy::Layer styled_layer(document.allocate_layer_id(), "Checkbox", std::move(pixels));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(patchy::Rect{4, 4, 32, 32});

  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = patchy::BlendMode::Normal;
  overlay.color = patchy::RgbColor{255, 242, 0};
  overlay.opacity = 1.0F;
  layer.layer_style().color_overlays.push_back(overlay);

  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.highlight_blend_mode = patchy::BlendMode::Screen;
  bevel.highlight_color = patchy::RgbColor{255, 255, 255};
  bevel.highlight_opacity = 0.75F;
  bevel.shadow_blend_mode = patchy::BlendMode::Multiply;
  bevel.shadow_color = patchy::RgbColor{0, 0, 0};
  bevel.shadow_opacity = 0.75F;
  bevel.angle_degrees = 120.0F;
  bevel.altitude_degrees = 30.0F;
  bevel.depth = 1.0F;
  bevel.size = 5.0F;
  bevel.direction_up = true;
  layer.layer_style().bevels.push_back(bevel);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* top_center = flattened.pixel(20, 6);
  const auto* inner_top_edge = flattened.pixel(20, 8);
  const auto* left_center = flattened.pixel(6, 20);
  const auto* inner_left_edge = flattened.pixel(8, 20);

  CHECK(inner_top_edge[1] + 20 < top_center[1]);
  CHECK(inner_left_edge[1] + 20 < left_center[1]);

  int continuous_inner_top_shadow = 0;
  for (std::int32_t x = 12; x <= 28; ++x) {
    const auto* px = flattened.pixel(x, 8);
    if (px[1] + 20 < top_center[1]) {
      ++continuous_inner_top_shadow;
    }
  }
  CHECK(continuous_inner_top_shadow >= 14);
  write_rgb8_bmp_artifact("layer_style_bevel_checkbox", flattened);
}

void compositor_renders_layer_style_outer_glow() {
  CHECK(patchy::LayerOuterGlow{}.blend_mode == patchy::BlendMode::Normal);

  patchy::Document document(14, 14, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(14, 14, 255, 255, 255));

  patchy::Layer styled_layer(document.allocate_layer_id(), "Glow", solid_rgba(4, 4, 20, 20, 220, 255));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(patchy::Rect{5, 5, 4, 4});

  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{255, 0, 0};
  glow.opacity = 1.0F;
  glow.spread = 100.0F;
  glow.size = 4.0F;
  layer.layer_style().outer_glows.push_back(glow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* glow_px = flattened.pixel(4, 6);
  const auto* layer_px = flattened.pixel(6, 6);
  CHECK(glow_px[0] > 240);
  CHECK(glow_px[1] < 120);
  CHECK(glow_px[2] < 120);
  CHECK(layer_px[2] > 200);
}

void compositor_outer_glow_spread_stays_within_size() {
  patchy::Document document(64, 32, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(64, 32, 255, 255, 255));

  patchy::Layer styled_layer(document.allocate_layer_id(), "Glow", solid_rgba(4, 4, 20, 20, 220, 255));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(patchy::Rect{30, 14, 4, 4});

  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{255, 0, 0};
  glow.opacity = 1.0F;
  glow.spread = 100.0F;
  glow.size = 6.0F;
  layer.layer_style().outer_glows.push_back(glow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* inside_glow_px = flattened.pixel(24, 15);
  const auto* outside_size_px = flattened.pixel(22, 15);
  CHECK(inside_glow_px[1] < 40);
  CHECK(outside_size_px[1] > 240);
  CHECK(outside_size_px[2] > 240);
}

void compositor_outer_glow_spread_uses_circular_distance_field() {
  patchy::Document document(28, 28, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(28, 28, 255, 255, 255));

  patchy::Layer styled_layer(document.allocate_layer_id(), "Glow", solid_rgba(4, 4, 20, 20, 220, 255));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(patchy::Rect{12, 12, 4, 4});

  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{255, 0, 0};
  glow.opacity = 1.0F;
  glow.spread = 100.0F;
  glow.size = 6.0F;
  layer.layer_style().outer_glows.push_back(glow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* inside_radius = flattened.pixel(6, 13);
  const auto* outside_diagonal = flattened.pixel(7, 7);
  CHECK(inside_radius[0] > 240);
  CHECK(inside_radius[1] < 40);
  CHECK(inside_radius[2] < 40);
  CHECK(outside_diagonal[0] > 240);
  CHECK(outside_diagonal[1] > 240);
  CHECK(outside_diagonal[2] > 240);
}

void compositor_drop_shadow_preserves_source_alpha() {
  patchy::Document document(5, 3, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(5, 3, 255, 255, 255));

  patchy::Layer layer(document.allocate_layer_id(), "Half Alpha", solid_rgba(1, 1, 220, 20, 20, 128));
  auto& source = document.add_layer(std::move(layer));
  source.set_bounds(patchy::Rect{1, 1, 1, 1});

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Normal;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 180.0F;
  shadow.distance = 1.0F;
  shadow.size = 0.0F;
  source.layer_style().drop_shadows.push_back(shadow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* shadow_px = flattened.pixel(2, 1);
  CHECK(shadow_px[0] > 110);
  CHECK(shadow_px[0] < 150);
  CHECK(shadow_px[1] > 110);
  CHECK(shadow_px[1] < 150);
  CHECK(shadow_px[2] > 110);
  CHECK(shadow_px[2] < 150);
}

void compositor_drop_shadow_preserves_connected_antialias_alpha() {
  patchy::Document document(12, 8, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(12, 8, 255, 255, 255));

  auto pixels = solid_rgba(4, 3, 220, 20, 20, 0);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < 3; ++x) {
      pixels.pixel(x, y)[3] = 255;
    }
    pixels.pixel(3, y)[3] = 64;
  }

  patchy::Layer layer(document.allocate_layer_id(), "Connected Antialias", std::move(pixels));
  auto& source = document.add_layer(std::move(layer));
  source.set_bounds(patchy::Rect{1, 2, 4, 3});

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Normal;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 180.0F;
  shadow.distance = 3.0F;
  shadow.size = 0.0F;
  source.layer_style().drop_shadows.push_back(shadow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* antialias_shadow = flattened.pixel(7, 3);
  CHECK(antialias_shadow[0] > 175);
  CHECK(antialias_shadow[0] < 210);
  CHECK(antialias_shadow[1] > 175);
  CHECK(antialias_shadow[1] < 210);
  CHECK(antialias_shadow[2] > 175);
  CHECK(antialias_shadow[2] < 210);
}

void compositor_drop_shadow_soft_mask_has_smooth_falloff() {
  patchy::Document document(180, 120, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(180, 120, 255, 255, 255));

  auto pixels = solid_rgba(90, 70, 235, 35, 24, 0);
  const auto stamp_disc = [&pixels](float center_x, float center_y) {
    constexpr float radius = 2.6F;
    constexpr float edge_width = 0.9F;
    const auto min_x = std::max(0, static_cast<int>(std::floor(center_x - radius - edge_width)));
    const auto min_y = std::max(0, static_cast<int>(std::floor(center_y - radius - edge_width)));
    const auto max_x = std::min(pixels.width() - 1, static_cast<int>(std::ceil(center_x + radius + edge_width)));
    const auto max_y = std::min(pixels.height() - 1, static_cast<int>(std::ceil(center_y + radius + edge_width)));
    for (std::int32_t y = min_y; y <= max_y; ++y) {
      for (std::int32_t x = min_x; x <= max_x; ++x) {
        const auto dx = static_cast<float>(x) + 0.5F - center_x;
        const auto dy = static_cast<float>(y) + 0.5F - center_y;
        const auto distance = std::sqrt(dx * dx + dy * dy);
        const auto coverage = patchy::clamp_unit((radius + edge_width - distance) / (2.0F * edge_width));
        if (coverage <= 0.0F) {
          continue;
        }
        auto* px = pixels.pixel(x, y);
        px[3] = std::max(px[3], static_cast<std::uint8_t>(std::lround(coverage * 255.0F)));
      }
    }
  };

  for (int step = 0; step < 96; ++step) {
    const auto t = static_cast<float>(step) / 95.0F;
    const auto center_x = 45.0F + std::sin(t * 5.1F) * 20.0F;
    const auto center_y = 8.0F + t * 54.0F;
    stamp_disc(center_x, center_y);
  }

  patchy::Layer layer(document.allocate_layer_id(), "Soft Shadow Curve", std::move(pixels));
  auto& source = document.add_layer(std::move(layer));
  source.set_bounds(patchy::Rect{55, 25, 90, 70});

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Normal;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 180.0F;
  shadow.distance = 0.0F;
  shadow.spread = 23.0F;
  shadow.size = 40.0F;
  source.layer_style().drop_shadows.push_back(shadow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  std::vector<int> intermediate_levels;
  int max_adjacent_gray_delta = 0;
  for (std::int32_t y = 5; y < flattened.height() - 5; ++y) {
    bool previous_gray = false;
    int previous_value = 255;
    for (std::int32_t x = 5; x < flattened.width() - 5; ++x) {
      const auto* px = flattened.pixel(x, y);
      const bool is_gray = std::abs(static_cast<int>(px[0]) - static_cast<int>(px[1])) <= 1 &&
                           std::abs(static_cast<int>(px[1]) - static_cast<int>(px[2])) <= 1;
      if (!is_gray) {
        previous_gray = false;
        previous_value = 255;
        continue;
      }

      const auto value = static_cast<int>(px[0]);
      if (value > 25 && value < 250) {
        intermediate_levels.push_back(value);
      }
      if (previous_gray && (value < 252 || previous_value < 252)) {
        max_adjacent_gray_delta = std::max(max_adjacent_gray_delta, std::abs(value - previous_value));
      }
      previous_gray = true;
      previous_value = value;
    }
  }
  std::sort(intermediate_levels.begin(), intermediate_levels.end());
  intermediate_levels.erase(std::unique(intermediate_levels.begin(), intermediate_levels.end()),
                            intermediate_levels.end());
  CHECK(intermediate_levels.size() >= 32U);
  CHECK(max_adjacent_gray_delta <= 35);
  write_rgb8_bmp_artifact("drop_shadow_soft_mask_smooth_falloff", flattened);
}

void compositor_outer_glow_preserves_source_alpha() {
  auto make_document = [](std::uint8_t alpha) {
    patchy::Document document(11, 11, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(11, 11, 0, 0, 0));
    patchy::Layer layer(document.allocate_layer_id(), "Glow Source", solid_rgba(1, 1, 20, 20, 20, alpha));
    auto& source = document.add_layer(std::move(layer));
    source.set_bounds(patchy::Rect{5, 5, 1, 1});

    patchy::LayerOuterGlow glow;
    glow.enabled = true;
    glow.blend_mode = patchy::BlendMode::Normal;
    glow.color = patchy::RgbColor{255, 255, 255};
    glow.opacity = 1.0F;
    glow.size = 2.0F;
    source.layer_style().outer_glows.push_back(glow);
    return document;
  };

  const auto half_alpha = patchy::Compositor{}.flatten_rgb8(make_document(128));
  const auto opaque = patchy::Compositor{}.flatten_rgb8(make_document(255));
  const auto half_value = half_alpha.pixel(6, 5)[0];
  const auto opaque_value = opaque.pixel(6, 5)[0];
  CHECK(half_value > 5);
  CHECK(opaque_value > half_value + 6);
}

void compositor_outer_glow_antialias_strength_does_not_create_streaks() {
  patchy::Document document(56, 30, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(56, 30, 0, 0, 0));

  auto pixels = solid_rgba(14, 10, 255, 255, 255, 0);
  for (std::int32_t y = 1; y < 9; ++y) {
    pixels.pixel(1, y)[3] = 64;
  }
  for (std::int32_t index = 0; index < 6; ++index) {
    pixels.pixel(2 + index, 2 + index)[3] = 64;
  }
  for (std::int32_t y = 4; y < 7; ++y) {
    for (std::int32_t x = 8; x < 11; ++x) {
      pixels.pixel(x, y)[3] = 255;
    }
  }

  patchy::Layer styled_layer(document.allocate_layer_id(), "Antialias Source", std::move(pixels));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(patchy::Rect{24, 10, 14, 10});

  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{255, 255, 255};
  glow.opacity = 1.0F;
  glow.spread = 100.0F;
  glow.size = 8.0F;
  layer.layer_style().outer_glows.push_back(glow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* projected_edge_alpha = flattened.pixel(18, 14);
  CHECK(projected_edge_alpha[0] > 220);
  CHECK(projected_edge_alpha[1] > 220);
  CHECK(projected_edge_alpha[2] > 220);
}

void compositor_large_text_style_spread_keeps_rounded_silhouette() {
  patchy::Document document(240, 150, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(240, 150, 243, 237, 230));

  auto pixels = solid_rgba(180, 70, 255, 255, 255, 0);
  const auto fill_text_run = [&pixels](std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
    for (std::int32_t py = y; py < y + height; ++py) {
      for (std::int32_t px = x; px < x + width; ++px) {
        auto* pixel = pixels.pixel(px, py);
        pixel[0] = 255;
        pixel[1] = 255;
        pixel[2] = 255;
        pixel[3] = 255;
      }
    }
  };
  fill_text_run(18, 18, 52, 7);
  fill_text_run(84, 18, 58, 7);
  fill_text_run(48, 36, 72, 7);
  fill_text_run(75, 54, 36, 7);

  patchy::Layer styled_layer(document.allocate_layer_id(), "Styled Text", std::move(pixels));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(patchy::Rect{40, 35, 180, 70});

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Multiply;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 90.0F;
  shadow.distance = 14.0F;
  shadow.spread = 74.0F;
  shadow.size = 20.0F;
  layer.layer_style().drop_shadows.push_back(shadow);

  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{184, 81, 74};
  glow.opacity = 1.0F;
  glow.spread = 100.0F;
  glow.size = 20.0F;
  layer.layer_style().outer_glows.push_back(glow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* top_glow = flattened.pixel(80, 35);
  const auto* top_left_corner = flattened.pixel(45, 35);
  const auto* top_right_corner = flattened.pixel(198, 36);
  const auto* shadow_px = flattened.pixel(125, 106);

  CHECK(top_glow[0] >= 170);
  CHECK(top_glow[0] <= 200);
  CHECK(top_glow[1] < 120);
  CHECK(top_glow[2] < 120);
  CHECK(top_left_corner[0] > 235);
  CHECK(top_left_corner[1] > 229);
  CHECK(top_left_corner[2] > 222);
  CHECK(top_right_corner[0] > 235);
  CHECK(top_right_corner[1] > 229);
  CHECK(top_right_corner[2] > 222);
  CHECK(shadow_px[0] < 190);
  CHECK(shadow_px[1] < 185);
  CHECK(shadow_px[2] < 180);
}

void layer_style_spread_dilation_keeps_circular_radius() {
  constexpr int width = 7;
  constexpr int height = 7;
  std::vector<float> mask(static_cast<std::size_t>(width * height), 0.0F);
  mask[static_cast<std::size_t>(3 * width + 3)] = 1.0F;

  const auto dilated = patchy::render_detail::dilate_mask(mask, width, height, 2);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto dx = x - 3;
      const auto dy = y - 3;
      const auto inside_circle = dx * dx + dy * dy <= 4;
      const auto value = dilated[static_cast<std::size_t>(y * width + x)];
      CHECK(inside_circle ? value == 1.0F : value == 0.0F);
    }
  }
}

void visible_alpha_bounds_track_sparse_rgba_pixels() {
  auto pixels = solid_rgba(8, 6, 0, 0, 0, 0);
  pixels.pixel(2, 1)[3] = 1;
  pixels.pixel(5, 4)[3] = 128;

  const auto local_bounds = patchy::visible_alpha_local_bounds(pixels);
  CHECK(local_bounds.has_value());
  CHECK(local_bounds->x == 2);
  CHECK(local_bounds->y == 1);
  CHECK(local_bounds->width == 4);
  CHECK(local_bounds->height == 4);

  patchy::Layer layer(1, "Sparse", pixels);
  const auto document_bounds = patchy::layer_visible_alpha_bounds(layer, patchy::Rect{12, 20, 8, 6});
  CHECK(document_bounds.has_value());
  CHECK(document_bounds->x == 14);
  CHECK(document_bounds->y == 21);
  CHECK(document_bounds->width == 4);
  CHECK(document_bounds->height == 4);

  CHECK(!patchy::visible_alpha_local_bounds(solid_rgba(3, 2, 0, 0, 0, 0)).has_value());
  const auto opaque_rgb_bounds = patchy::visible_alpha_local_bounds(solid_rgb(4, 3, 10, 20, 30));
  CHECK(opaque_rgb_bounds.has_value());
  CHECK(opaque_rgb_bounds->x == 0);
  CHECK(opaque_rgb_bounds->y == 0);
  CHECK(opaque_rgb_bounds->width == 4);
  CHECK(opaque_rgb_bounds->height == 3);
}

void layer_style_preview_marks_large_or_broad_effects_expensive() {
  patchy::Layer plain_layer(1, "Plain", solid_rgba(10, 10, 20, 20, 20, 255));
  plain_layer.set_bounds(patchy::Rect{100, 100, 10, 10});
  CHECK(!patchy::layer_style_preview_is_expensive(plain_layer, patchy::Rect::from_size(1000, 1000)));

  patchy::Layer large_glow_layer(2, "Large Glow", solid_rgba(10, 10, 20, 20, 20, 255));
  large_glow_layer.set_bounds(patchy::Rect{100, 100, 10, 10});
  patchy::LayerOuterGlow large_glow;
  large_glow.enabled = true;
  large_glow.opacity = 1.0F;
  large_glow.size = 221.0F;
  large_glow.spread = 14.0F;
  large_glow_layer.layer_style().outer_glows.push_back(large_glow);
  CHECK(patchy::layer_style_preview_is_expensive(large_glow_layer, patchy::Rect::from_size(1000, 1000)));

  patchy::Layer broad_layer(3, "Broad", solid_rgba(50, 50, 20, 20, 20, 255));
  broad_layer.set_bounds(patchy::Rect{10, 10, 50, 50});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.opacity = 1.0F;
  stroke.size = 1.0F;
  broad_layer.layer_style().strokes.push_back(stroke);
  CHECK(patchy::layer_style_preview_is_expensive(broad_layer, patchy::Rect::from_size(100, 100)));
}

void compositor_renders_sparse_outer_glow_from_visible_alpha_bounds() {
  const auto make_document = [](bool cropped_source) {
    patchy::Document document(160, 120, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(160, 120, 255, 255, 255));

    patchy::PixelBuffer pixels = cropped_source ? solid_rgba(8, 8, 20, 20, 220, 255)
                                                : solid_rgba(90, 50, 0, 0, 0, 0);
    if (!cropped_source) {
      for (std::int32_t y = 15; y < 23; ++y) {
        for (std::int32_t x = 35; x < 43; ++x) {
          auto* px = pixels.pixel(x, y);
          px[0] = 20;
          px[1] = 20;
          px[2] = 220;
          px[3] = 255;
        }
      }
    }

    patchy::Layer layer(document.allocate_layer_id(), "Glow", std::move(pixels));
    layer.set_bounds(cropped_source ? patchy::Rect{65, 35, 8, 8} : patchy::Rect{30, 20, 90, 50});
    patchy::LayerOuterGlow glow;
    glow.enabled = true;
    glow.blend_mode = patchy::BlendMode::Normal;
    glow.color = patchy::RgbColor{255, 0, 0};
    glow.opacity = 0.85F;
    glow.spread = 25.0F;
    glow.size = 18.0F;
    layer.layer_style().outer_glows.push_back(glow);
    document.add_layer(std::move(layer));
    return document;
  };

  const auto sparse = patchy::Compositor{}.flatten_rgb8(make_document(false));
  const auto cropped = patchy::Compositor{}.flatten_rgb8(make_document(true));
  for (std::int32_t y = 0; y < sparse.height(); ++y) {
    for (std::int32_t x = 0; x < sparse.width(); ++x) {
      const auto* sparse_px = sparse.pixel(x, y);
      const auto* cropped_px = cropped.pixel(x, y);
      for (int channel = 0; channel < 3; ++channel) {
        CHECK(std::abs(static_cast<int>(sparse_px[channel]) - static_cast<int>(cropped_px[channel])) <= 1);
      }
    }
  }
}

void compositor_renders_sparse_drop_shadow_from_visible_alpha_bounds() {
  const auto make_document = [](bool cropped_source) {
    patchy::Document document(160, 120, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(160, 120, 255, 255, 255));

    patchy::PixelBuffer pixels = cropped_source ? solid_rgba(8, 8, 30, 80, 220, 255)
                                                : solid_rgba(90, 50, 0, 0, 0, 0);
    if (!cropped_source) {
      for (std::int32_t y = 15; y < 23; ++y) {
        for (std::int32_t x = 35; x < 43; ++x) {
          auto* px = pixels.pixel(x, y);
          px[0] = 30;
          px[1] = 80;
          px[2] = 220;
          px[3] = 255;
        }
      }
    }

    patchy::Layer layer(document.allocate_layer_id(), "Shadow", std::move(pixels));
    layer.set_bounds(cropped_source ? patchy::Rect{65, 35, 8, 8} : patchy::Rect{30, 20, 90, 50});
    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.blend_mode = patchy::BlendMode::Normal;
    shadow.color = patchy::RgbColor{0, 0, 0};
    shadow.opacity = 0.85F;
    shadow.angle_degrees = 135.0F;
    shadow.distance = 7.0F;
    shadow.size = 18.0F;
    shadow.spread = 25.0F;
    layer.layer_style().drop_shadows.push_back(shadow);
    document.add_layer(std::move(layer));
    return document;
  };

  const auto sparse = patchy::Compositor{}.flatten_rgb8(make_document(false));
  const auto cropped = patchy::Compositor{}.flatten_rgb8(make_document(true));
  for (std::int32_t y = 0; y < sparse.height(); ++y) {
    for (std::int32_t x = 0; x < sparse.width(); ++x) {
      const auto* sparse_px = sparse.pixel(x, y);
      const auto* cropped_px = cropped.pixel(x, y);
      for (int channel = 0; channel < 3; ++channel) {
        CHECK(std::abs(static_cast<int>(sparse_px[channel]) - static_cast<int>(cropped_px[channel])) <= 1);
      }
    }
  }
}

void compositor_renders_layer_style_color_overlay() {
  patchy::Document document(4, 4, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(4, 4, 255, 255, 255));

  auto& layer = document.add_pixel_layer("Overridden", solid_rgba(4, 4, 20, 40, 220, 255));
  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = patchy::BlendMode::Normal;
  overlay.color = patchy::RgbColor{240, 60, 20};
  overlay.opacity = 1.0F;
  layer.layer_style().color_overlays.push_back(overlay);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* px = flattened.pixel(1, 1);
  CHECK(px[0] == 240);
  CHECK(px[1] == 60);
  CHECK(px[2] == 20);
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

void layer_mask_shapes_effects_regardless_of_link() {
  // Photoshop shapes layer effects with the layer mask whether or not the mask
  // is linked (verified against Photoshop 2026 with both drop shadows and
  // strokes): the chain toggle only controls move behavior, not rendering.
  for (const auto linked : {true, false}) {
    patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(64, 64, 255, 255, 255));
    patchy::Layer shadowed(document.allocate_layer_id(), "Shadowed", solid_rgba(16, 16, 200, 30, 30, 255));
    shadowed.set_bounds(patchy::Rect{16, 16, 16, 16});
    patchy::PixelBuffer mask_pixels(64, 64, patchy::PixelFormat::gray8());
    mask_pixels.clear(0);  // hide the whole layer
    shadowed.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 64, 64}, std::move(mask_pixels), 0, false});
    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.opacity = 1.0F;
    shadow.angle_degrees = 90.0F;  // shadow straight down
    shadow.distance = 20.0F;
    shadow.size = 2.0F;
    shadowed.layer_style().drop_shadows.push_back(shadow);
    patchy::set_layer_mask_linked(shadowed, linked);
    document.add_layer(std::move(shadowed));

    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    // The square sits at (16..32)^2; its shadow would land around (16..32)x(36..52).
    CHECK(flattened.pixel(24, 24)[0] > 240);  // content hidden
    CHECK(flattened.pixel(24, 44)[0] > 240);  // hidden content casts no shadow either
  }

  // Photoshop-authored reference: unlinked mask (reveal rect (24,8)-(56,40),
  // default 0) over a red square at (16..48)^2 with a black drop shadow
  // (down 10px, blur 4). Expectations measured from Photoshop 2026's render.
  const auto fixture_path = patchy::test::committed_psd_fixture_path("photoshop-unlinked-mask-shadow.psd");
  const auto fixture = patchy::psd::DocumentIo::read_file(fixture_path);
  const auto* layer = find_layer_named(fixture.layers(), "Layer 1");
  CHECK(layer != nullptr);
  CHECK(!patchy::layer_mask_linked(*layer));
  const auto rendered = patchy::Compositor{}.flatten_rgb8(fixture);
  CHECK(rendered.pixel(40, 30)[0] > 150 && rendered.pixel(40, 30)[1] < 90);  // revealed red content
  CHECK(rendered.pixel(20, 30)[0] > 240);                                    // content hidden by default color
  CHECK(rendered.pixel(40, 44)[0] < 90);                                     // shadow of revealed content
  CHECK(rendered.pixel(40, 53)[0] > 240);  // no shadow where the mask hides the source
}

void psd_layer_mask_hides_effects_round_trip() {
  for (const auto hides : {false, true}) {
    patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
    auto& layer = document.add_pixel_layer("Styled", solid_rgba(4, 2, 10, 20, 30, 255));
    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    layer.layer_style().drop_shadows.push_back(shadow);
    layer.layer_style().layer_mask_hides_effects = hides;

    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    const auto payload = psd_layer_block_payload(psd_first_layer_extra_data(bytes), "lmgm");
    CHECK(payload.has_value() == hides);
    if (payload.has_value()) {
      CHECK(payload->size() == 4U);
      CHECK((*payload)[0] == 1U);
    }

    const auto read = patchy::psd::DocumentIo::read(bytes);
    CHECK(read.layers().size() == 1);
    CHECK(read.layers().front().layer_style().layer_mask_hides_effects == hides);
  }
}

void layer_mask_hides_effects_clips_exterior_effect_output() {
  // "Layer Mask Hides Effects" off (default): the mask shapes what casts the
  // effect, but shadow/glow/stroke output may still land on mask-hidden areas.
  // On: the mask also clips the effect output (Photoshop's Blending Options box).
  for (const auto hides : {false, true}) {
    patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(64, 64, 255, 255, 255));
    patchy::Layer shadowed(document.allocate_layer_id(), "Shadowed", solid_rgba(16, 16, 200, 30, 30, 255));
    shadowed.set_bounds(patchy::Rect{16, 16, 16, 16});
    patchy::PixelBuffer mask_pixels(64, 64, patchy::PixelFormat::gray8());
    for (std::int32_t y = 0; y < 64; ++y) {
      for (std::int32_t x = 0; x < 64; ++x) {
        *mask_pixels.pixel(x, y) = x < 32 ? 255 : 0;  // hide the canvas right half
      }
    }
    shadowed.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 64, 64}, std::move(mask_pixels), 0, false});
    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.opacity = 1.0F;
    shadow.angle_degrees = 180.0F;  // light from the left; shadow lands 20px to the right
    shadow.distance = 20.0F;
    shadow.size = 2.0F;
    shadowed.layer_style().drop_shadows.push_back(shadow);
    shadowed.layer_style().layer_mask_hides_effects = hides;
    document.add_layer(std::move(shadowed));

    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    CHECK(flattened.pixel(24, 24)[0] > 150 && flattened.pixel(24, 24)[1] < 90);  // visible red square
    // The square (16..32)^2 casts its shadow at (36..52)x(16..32), inside the
    // mask-hidden half of the canvas.
    if (hides) {
      CHECK(flattened.pixel(44, 24)[0] > 240);  // output clipped by the mask
    } else {
      CHECK(flattened.pixel(44, 24)[0] < 60);  // output spills onto hidden area
    }
  }
}

void psd_photoshop_mask_hides_effects_fixture_clips_shadow() {
  // Same Photoshop document as photoshop-global-light-shadow.psd, saved with
  // "Layer Mask Hides Effects" checked ('lmgm'). The mask hides x >= 32; the
  // shadow body sits at x16..31 with blur spill just past the boundary.
  const auto clipped = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-mask-hides-effects.psd"));
  const auto* layer = find_layer_named(clipped.layers(), "Layer 1");
  CHECK(layer != nullptr);
  CHECK(layer->layer_style().layer_mask_hides_effects);
  const auto clipped_render = patchy::Compositor{}.flatten_rgb8(clipped);
  CHECK(clipped_render.pixel(24, 52)[0] < 110);   // shadow body
  CHECK(clipped_render.pixel(33, 52)[0] > 250);   // spill clipped at the mask boundary

  const auto spilling = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-global-light-shadow.psd"));
  const auto* spilling_layer = find_layer_named(spilling.layers(), "Layer 1");
  CHECK(spilling_layer != nullptr);
  CHECK(!spilling_layer->layer_style().layer_mask_hides_effects);
  const auto spilling_render = patchy::Compositor{}.flatten_rgb8(spilling);
  CHECK(spilling_render.pixel(33, 52)[0] < 250);  // blur spill crosses the boundary
}

void layer_stroke_outlines_semi_transparent_regions_without_fill() {
  // Content under a 50% gray layer mask must be stroked along its pixel contour
  // only, with the mask attenuating the stroke where it lands. The old formula
  // derived the stroke from alpha x mask, painting a constant wash across the
  // region's whole interior and hiding the half-visible content beneath;
  // Photoshop draws no stroke there.
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 64, 255, 255, 255));
  patchy::Layer stroked(document.allocate_layer_id(), "Stroked", solid_rgba(32, 32, 20, 90, 200, 255));
  stroked.set_bounds(patchy::Rect{16, 16, 32, 32});
  patchy::PixelBuffer mask_pixels(64, 64, patchy::PixelFormat::gray8());
  mask_pixels.clear(128);
  stroked.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 64, 64}, std::move(mask_pixels), 128, false});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.opacity = 1.0F;
  stroke.size = 3.0F;
  stroke.position = patchy::LayerStrokePosition::Outside;
  stroke.color = patchy::RgbColor{255, 0, 0};
  stroked.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(stroked));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  // Interior of the square: half-visible blue content, no red stroke wash.
  const auto* interior = flattened.pixel(32, 32);
  CHECK(interior[2] > 200);
  CHECK(interior[0] < 150);
  // Just outside the square: the stroke band attenuated to the mask's 50%.
  const auto* edge = flattened.pixel(14, 32);
  CHECK(edge[0] > 180);
  CHECK(edge[1] < 180);
}

void exact_squared_distance_transform_matches_bruteforce() {
  constexpr int width = 9;
  constexpr int height = 9;
  const std::array<std::pair<int, int>, 2> sources{{{2, 3}, {7, 8}}};
  std::vector<float> field(static_cast<std::size_t>(width) * height, patchy::render_detail::kEdtUnreached);
  for (const auto& [sx, sy] : sources) {
    field[static_cast<std::size_t>(sy) * width + sx] = 0.0F;
  }
  patchy::render_detail::exact_squared_distance_transform(field, width, height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int expected = 1 << 30;
      for (const auto& [sx, sy] : sources) {
        expected = std::min(expected, (x - sx) * (x - sx) + (y - sy) * (y - sy));
      }
      CHECK(field[static_cast<std::size_t>(y) * width + x] == static_cast<float>(expected));
    }
  }
}

void layer_stroke_center_band_width_matches_size() {
  // Photoshop's Center stroke spans size/2 outward + size/2 inward (total = size).
  // The legacy dilation grew size in BOTH directions, doubling the band.
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 64, 255, 255, 255));
  patchy::Layer shape(document.allocate_layer_id(), "Shape", solid_rgba(16, 16, 0, 0, 255, 255));
  shape.set_bounds(patchy::Rect{24, 24, 16, 16});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.opacity = 1.0F;
  stroke.size = 6.0F;
  stroke.position = patchy::LayerStrokePosition::Center;
  stroke.color = patchy::RgbColor{255, 0, 0};
  shape.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(shape));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  // Left edge at x=24: band covers x=21..23 outside and x=24..26 inside.
  for (int x = 21; x <= 26; ++x) {
    const auto* px = flattened.pixel(x, 32);
    CHECK(px[0] > 240 && px[1] < 15 && px[2] < 15);
  }
  const auto* beyond_outside = flattened.pixel(20, 32);
  CHECK(beyond_outside[0] > 240 && beyond_outside[1] > 240 && beyond_outside[2] > 240);
  const auto* beyond_inside = flattened.pixel(27, 32);
  CHECK(beyond_inside[2] > 240 && beyond_inside[0] < 15);
}

void layer_stroke_center_has_no_seam_on_antialiased_contour() {
  // A partially covered contour pixel sits in the middle of a Center band: the
  // inside and outside halves must sum to full stroke coverage there. The legacy
  // max() combine dipped to ~50%, leaving a bright seam along anti-aliased edges.
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 64, 255, 255, 255));
  auto pixels = solid_rgba(16, 16, 0, 0, 255, 255);
  for (std::int32_t y = 0; y < 16; ++y) {
    pixels.pixel(0, y)[3] = 128;  // anti-aliased left edge column
  }
  patchy::Layer shape(document.allocate_layer_id(), "Shape", std::move(pixels));
  shape.set_bounds(patchy::Rect{24, 24, 16, 16});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.opacity = 1.0F;
  stroke.size = 4.0F;
  stroke.position = patchy::LayerStrokePosition::Center;
  stroke.color = patchy::RgbColor{255, 0, 0};
  shape.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(shape));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  // The 50%-alpha contour column and its full-coverage neighbors are pure stroke.
  for (int x = 23; x <= 25; ++x) {
    const auto* px = flattened.pixel(x, 32);
    CHECK(px[0] > 240 && px[1] < 15 && px[2] < 15);
  }
}

void layer_stroke_outside_antialiases_corners() {
  // Band edges follow Euclidean distance with a 1px anti-aliasing ramp: rectangle
  // corners get partially covered pixels instead of the legacy hard staircase.
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 64, 255, 255, 255));
  patchy::Layer shape(document.allocate_layer_id(), "Shape", solid_rgba(8, 8, 0, 0, 255, 255));
  shape.set_bounds(patchy::Rect{24, 24, 8, 8});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.opacity = 1.0F;
  stroke.size = 2.0F;
  stroke.position = patchy::LayerStrokePosition::Outside;
  stroke.color = patchy::RgbColor{255, 0, 0};
  shape.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(shape));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  // Axis-aligned: full band at d <= 2, nothing at d = 3.
  const auto* on_band = flattened.pixel(24, 22);
  CHECK(on_band[0] > 240 && on_band[1] < 15);
  const auto* past_band = flattened.pixel(24, 21);
  CHECK(past_band[1] > 245);
  // Diagonal by the corner: d = sqrt(2) is full, d = 2*sqrt(2) ~ 2.83 is a partial
  // ~17% pixel (green channel ~211 over white), d = 3*sqrt(2) is untouched.
  const auto* diagonal_full = flattened.pixel(23, 23);
  CHECK(diagonal_full[0] > 240 && diagonal_full[1] < 15);
  const auto* diagonal_partial = flattened.pixel(22, 22);
  CHECK(diagonal_partial[1] > 185 && diagonal_partial[1] < 235);
  const auto* diagonal_outside = flattened.pixel(21, 21);
  CHECK(diagonal_outside[1] > 245);
}

void layer_stroke_fractional_size_renders_partial_ring() {
  // Fractional sizes land as partial coverage at the band edge instead of the
  // legacy ceil() snap to the next integer radius.
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 64, 255, 255, 255));
  patchy::Layer shape(document.allocate_layer_id(), "Shape", solid_rgba(8, 8, 0, 0, 255, 255));
  shape.set_bounds(patchy::Rect{24, 24, 8, 8});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.opacity = 1.0F;
  stroke.size = 2.5F;
  stroke.position = patchy::LayerStrokePosition::Outside;
  stroke.color = patchy::RgbColor{255, 0, 0};
  shape.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(shape));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* full = flattened.pixel(22, 28);  // d = 2 -> full coverage
  CHECK(full[0] > 240 && full[1] < 15);
  const auto* half = flattened.pixel(21, 28);  // d = 3 -> 50% coverage over white
  CHECK(half[1] > 100 && half[1] < 155);
  const auto* outside = flattened.pixel(20, 28);  // d = 4 -> untouched
  CHECK(outside[1] > 245);
}

void psd_photoshop_stroke_positions_fixture_matches() {
  // Photoshop-authored reference: three opaque red rects (y16..47) with a green
  // size-6 stroke at each position — outside (x8..39), center (x64..95), inside
  // (x120..151). Band expectations measured from Photoshop 2026's render, which
  // Patchy's distance-band stroke matches run-for-run: outside spans d=1..6
  // beyond the contour, center splits 3+3, inside spans d=1..6 within.
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-stroke-positions.psd"));
  const auto rendered = patchy::Compositor{}.flatten_rgb8(document);
  const auto expect_green = [&](int x, int y) {
    const auto* px = rendered.pixel(x, y);
    CHECK(px[1] > 150 && px[0] < 110);
  };
  const auto expect_red = [&](int x, int y) {
    const auto* px = rendered.pixel(x, y);
    CHECK(px[0] > 150 && px[1] < 110);
  };
  const auto expect_white = [&](int x, int y) {
    const auto* px = rendered.pixel(x, y);
    CHECK(px[0] > 240 && px[1] > 240 && px[2] > 240);
  };
  // Outside: 6px band strictly beyond the contour.
  expect_white(1, 32);
  expect_green(2, 32);
  expect_green(7, 32);
  expect_red(8, 32);
  expect_red(24, 32);
  // Center: 3px out + 3px in.
  expect_white(60, 32);
  expect_green(61, 32);
  expect_green(66, 32);
  expect_red(67, 32);
  expect_red(80, 32);
  expect_red(92, 32);
  expect_green(93, 32);
  expect_green(98, 32);
  expect_white(99, 32);
  // Inside: 6px band strictly within the contour.
  expect_white(119, 32);
  expect_green(120, 32);
  expect_green(125, 32);
  expect_red(126, 32);
  expect_red(140, 32);
  // Vertical band through the outside rect (top and bottom edges).
  expect_green(36, 10);
  expect_green(36, 15);
  expect_red(36, 16);
  expect_red(36, 47);
  expect_green(36, 48);
  expect_green(36, 53);
  expect_white(36, 54);
}

void psd_photoshop_stroke_partial_alpha_fixture_matches() {
  // Photoshop-authored reference: regions painted at 100% (x8..28), 50% (x28..56),
  // and 25% alpha (y40..52) with a green 3px outside stroke. Photoshop treats any
  // painted pixel as inside the stroked shape — the stroke fills the binary shape
  // and the content covers it per its alpha — so semi-transparent regions show a
  // green wash, while the opaque region stays clean. Expectations measured from
  // Photoshop 2026's render.
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-stroke-partial-alpha.psd"));
  const auto rendered = patchy::Compositor{}.flatten_rgb8(document);
  const auto* opaque = rendered.pixel(16, 24);
  CHECK(opaque[0] > 150 && opaque[1] < 110);  // 100% region: pure content, no stroke
  const auto* half = rendered.pixel(44, 24);
  CHECK(half[1] > 150 && half[1] > half[0] + 30);  // 50% region: stroke shows through
  const auto* quarter = rendered.pixel(32, 46);
  CHECK(quarter[1] > 200 && quarter[0] < 110);  // 25% region: stroke dominates
  const auto* band = rendered.pixel(6, 24);
  CHECK(band[1] > 200 && band[0] < 80);  // outer band at full strength
}

void psd_global_link_blocks_round_trip_with_smart_object_layers() {
  patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer("Smart", solid_rgb(4, 2, 10, 20, 30));
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"PlLd", {1, 2, 3, 4}});
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"SoLd", {5, 6, 7, 8}});
  document.metadata().unknown_psd_resources.push_back(
      patchy::UnknownPsdBlock{"lnk2", {9, 9, 9, 9, 9, 9, 9, 9}});
  document.metadata().unknown_psd_resources.push_back(patchy::UnknownPsdBlock{"cinf", {1, 2, 3}});

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  // With the global link data preserved, the smart object references stay valid
  // and must be written too.
  const auto extra = psd_first_layer_extra_data(bytes);
  CHECK(psd_layer_block_payload(extra, "PlLd").has_value());
  CHECK(psd_layer_block_payload(extra, "SoLd").has_value());

  const auto read = patchy::psd::DocumentIo::read(bytes);
  // 'lnk*' globals now parse into the smart-object store; this synthetic payload is
  // not a valid element list, so it must be preserved as an OPAQUE store block.
  const auto& globals = read.metadata().unknown_psd_resources;
  CHECK(globals.size() == 1);
  CHECK(globals[0].key == "cinf");  // odd-sized payload exercises 4-byte padding
  CHECK(globals[0].payload == (std::vector<std::uint8_t>{1, 2, 3}));
  const auto& store = read.metadata().smart_objects;
  CHECK(store.blocks.size() == 1);
  CHECK(store.blocks[0].key == "lnk2");
  CHECK(store.blocks[0].opaque);
  CHECK(store.blocks[0].original_payload != nullptr);
  CHECK(*store.blocks[0].original_payload == (std::vector<std::uint8_t>{9, 9, 9, 9, 9, 9, 9, 9}));
  CHECK(read.layers().size() == 1);
  const auto& read_blocks = read.layers().front().unknown_psd_blocks();
  CHECK(std::any_of(read_blocks.begin(), read_blocks.end(),
                    [](const patchy::UnknownPsdBlock& block) { return block.key == "PlLd"; }));

  // The opaque block re-emits verbatim (still 'lnk2' + the same payload) on resave.
  const auto resaved = patchy::psd::DocumentIo::write_layered_rgb8(read);
  const auto reread = patchy::psd::DocumentIo::read(resaved);
  CHECK(reread.metadata().smart_objects.blocks.size() == 1);
  CHECK(*reread.metadata().smart_objects.blocks[0].original_payload ==
        (std::vector<std::uint8_t>{9, 9, 9, 9, 9, 9, 9, 9}));
}

void psd_dangling_smart_object_blocks_are_stripped() {
  patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer("Smart", solid_rgb(4, 2, 10, 20, 30));
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"PlLd", {1, 2, 3, 4}});
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"SoLd", {5, 6, 7, 8}});
  layer.unknown_psd_blocks().push_back(
      patchy::UnknownPsdBlock{"fxrp", std::vector<std::uint8_t>(16, 0)});

  // Without document-global 'lnk2' data the smart object references would dangle,
  // producing a file Photoshop can open but not save ("disk error (-1)").
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto extra = psd_first_layer_extra_data(bytes);
  CHECK(!psd_layer_block_payload(extra, "PlLd").has_value());
  CHECK(!psd_layer_block_payload(extra, "SoLd").has_value());
  CHECK(psd_layer_block_payload(extra, "fxrp").has_value());
}

void psd_smart_object_sources_survive_resave_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("eon_spider_original.psd");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] eon_spider_original fixture missing: " << path.string() << '\n';
    return;
  }
  const auto has_lnk2 = [](const patchy::Document& document) {
    const auto& store = document.metadata().smart_objects;
    return std::any_of(store.blocks.begin(), store.blocks.end(),
                       [](const patchy::SmartObjectLinkBlock& block) {
                         return block.key == "lnk2" && (!block.sources.empty() || block.opaque);
                       });
  };
  const auto document = patchy::psd::DocumentIo::read_file(path);
  CHECK(has_lnk2(document));
  const auto resaved = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  CHECK(has_lnk2(resaved));
}

bool bytes_contain_sequence(std::span<const std::uint8_t> haystack, std::string_view needle) {
  return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end()) != haystack.end();
}

void psb_layered_round_trip_preserves_layers_and_blocks() {
  patchy::Document document(6, 4, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(6, 4, 200, 30, 40));
  patchy::Layer top(document.allocate_layer_id(), "Top", solid_rgba(3, 2, 10, 20, 30, 128));
  top.set_bounds(patchy::Rect{1, 1, 3, 2});
  top.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"fxrp", std::vector<std::uint8_t>(16, 7)});
  document.add_layer(std::move(top));
  document.metadata().unknown_psd_resources.push_back(
      patchy::UnknownPsdBlock{"lnk2", {9, 9, 9, 9, 9, 9, 9, 9}});
  document.metadata().unknown_psd_resources.push_back(patchy::UnknownPsdBlock{"cinf", {1, 2, 3}});

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document, patchy::psd::WriteOptions{true});
  CHECK(bytes.size() > 6 && bytes[4] == 0 && bytes[5] == 2);  // header version 2 = PSB
  // 'lnk2' and 'cinf' are in the PSB 8-byte-length key set (cinf empirically: Photoshop
  // 2026 requires it wide in PSBs), so they carry the 8B64 signature + u64 length; keys
  // outside the set keep the 8BIM + u32 form ('fxrp' on the layer covers that path).
  CHECK(bytes_contain_sequence(bytes, "8B64lnk2"));
  CHECK(!bytes_contain_sequence(bytes, "8BIMlnk2"));
  CHECK(bytes_contain_sequence(bytes, "8B64cinf"));
  CHECK(bytes_contain_sequence(bytes, "8BIMfxrp"));

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.metadata().values.at("psd.version") == "PSB");
  CHECK(read.layers().size() == 2);
  const auto* base = find_layer_named(read.layers(), "Base");
  const auto* top_read = find_layer_named(read.layers(), "Top");
  CHECK(base != nullptr && top_read != nullptr);
  const auto* base_px = base->pixels().pixel(3, 2);
  CHECK(base_px[0] == 200 && base_px[1] == 30 && base_px[2] == 40);
  CHECK(top_read->bounds().x == 1 && top_read->bounds().y == 1 && top_read->bounds().width == 3 &&
        top_read->bounds().height == 2);
  const auto* top_px = top_read->pixels().pixel(0, 0);
  CHECK(top_px[0] == 10 && top_px[1] == 20 && top_px[2] == 30 && top_px[3] == 128);
  const auto& top_blocks = top_read->unknown_psd_blocks();
  CHECK(std::any_of(top_blocks.begin(), top_blocks.end(), [](const patchy::UnknownPsdBlock& block) {
    return block.key == "fxrp" && block.payload == std::vector<std::uint8_t>(16, 7);
  }));
  const auto& globals = read.metadata().unknown_psd_resources;
  CHECK(globals.size() == 1);
  CHECK(globals[0].key == "cinf");
  CHECK(globals[0].payload == (std::vector<std::uint8_t>{1, 2, 3}));
  CHECK(globals[0].long_length);
  const auto& store = read.metadata().smart_objects;
  CHECK(store.blocks.size() == 1);
  CHECK(store.blocks[0].key == "lnk2");
  CHECK(store.blocks[0].long_length);  // signature-derived: 8B64 blocks re-emit wide
  CHECK(store.blocks[0].opaque);
  CHECK(*store.blocks[0].original_payload == (std::vector<std::uint8_t>{9, 9, 9, 9, 9, 9, 9, 9}));
}

void psb_flat_round_trip_reads_composite() {
  // A flat PSB exercises the merged-image path, whose RLE row byte counts widen
  // to u32 in PSB (a 64px-wide solid row compresses, so RLE wins).
  patchy::Document document(64, 8, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 8, 12, 200, 99));
  const auto bytes = patchy::psd::DocumentIo::write_flat_rgb8(document, patchy::psd::WriteOptions{true});
  CHECK(bytes.size() > 6 && bytes[5] == 2);
  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.metadata().values.at("psd.version") == "PSB");
  CHECK(read.width() == 64 && read.height() == 8);
  CHECK(read.layers().size() == 1);
  const auto* px = read.layers().front().pixels().pixel(63, 7);
  CHECK(px[0] == 12 && px[1] == 200 && px[2] == 99);
}

void psb_photoshop_fixture_round_trips() {
  // Photoshop 2026-authored PSB (COM script; see AGENTS.md). Pins reading a real PS PSB —
  // including the 8B64-signature global blocks ('cinf' carries an 8-byte length there).
  const auto document =
      patchy::psd::DocumentIo::read_file(patchy::test::committed_psd_fixture_path("photoshop-basic.psb"));
  CHECK(document.metadata().values.at("psd.version") == "PSB");
  CHECK(document.width() == 40 && document.height() == 30);
  CHECK(document.layers().size() == 2);
  const auto* red = find_layer_named(document.layers(), "Red");
  CHECK(red != nullptr);
  CHECK(red->bounds().x == 4 && red->bounds().y == 4 && red->bounds().width == 16 && red->bounds().height == 12);
  const auto* px = red->pixels().pixel(2, 2);
  CHECK(px[0] == 210 && px[1] == 40 && px[2] == 50);
  const auto& globals = document.metadata().unknown_psd_resources;
  const auto cinf = std::find_if(globals.begin(), globals.end(),
                                 [](const patchy::UnknownPsdBlock& block) { return block.key == "cinf"; });
  CHECK(cinf != globals.end());
  CHECK(cinf->long_length);
  CHECK(cinf->payload.size() == 413);

  // The resave must keep Photoshop's 8B64 form for those blocks.
  const auto resaved = patchy::psd::DocumentIo::write_layered_rgb8(document, patchy::psd::WriteOptions{true});
  CHECK(bytes_contain_sequence(resaved, "8B64cinf"));
  CHECK(!bytes_contain_sequence(resaved, "8BIMcinf"));
}

// Parses a layer's 'SoLd' payload and re-serializes it through the generic descriptor
// writer; the result must be byte-identical to Photoshop's original (trailing bytes
// after the descriptor are 4-alignment padding and must be zero).
void check_sold_descriptor_round_trip(const patchy::Layer& layer) {
  const auto& blocks = layer.unknown_psd_blocks();
  const auto sold = std::find_if(blocks.begin(), blocks.end(),
                                 [](const patchy::UnknownPsdBlock& block) { return block.key == "SoLd"; });
  CHECK(sold != blocks.end());
  patchy::psd::BigEndianReader reader(sold->payload);
  CHECK(patchy::psd::key_string(patchy::psd::read_signature(reader)) == "soLD");
  const auto version = reader.read_u32();
  const auto descriptor_version = reader.read_u32();
  CHECK(version == 4);
  CHECK(descriptor_version == 16);
  const auto descriptor = patchy::psd::read_descriptor(reader);
  const auto consumed = reader.position();

  patchy::psd::BigEndianWriter writer;
  for (const char ch : {'s', 'o', 'L', 'D'}) {
    writer.write_u8(static_cast<std::uint8_t>(ch));
  }
  writer.write_u32(version);
  writer.write_u32(descriptor_version);
  patchy::psd::write_descriptor(writer, descriptor);
  const auto& rewritten = writer.bytes();

  std::size_t first_mismatch = std::string::npos;
  const auto compare_count = std::min(rewritten.size(), consumed);
  for (std::size_t i = 0; i < compare_count; ++i) {
    if (rewritten[i] != sold->payload[i]) {
      first_mismatch = i;
      break;
    }
  }
  if (first_mismatch != std::string::npos || rewritten.size() != consumed) {
    std::cout << "descriptor rewrite diverges: original consumed " << consumed << " bytes, rewrote "
              << rewritten.size() << ", first mismatch at "
              << (first_mismatch == std::string::npos ? compare_count : first_mismatch) << "\n";
  }
  CHECK(rewritten.size() == consumed);
  CHECK(first_mismatch == std::string::npos);
  for (std::size_t i = consumed; i < sold->payload.size(); ++i) {
    CHECK(sold->payload[i] == 0);
  }
}

void psd_smart_object_fixture_parses_placement_and_source() {
  std::vector<std::string> notices;
  patchy::psd::ReadOptions options;
  options.notices = &notices;
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-place-embedded-png.psd"), options);
  const auto* layer = find_layer_named(document.layers(), "small");
  CHECK(layer != nullptr);
  CHECK(patchy::layer_is_smart_object(*layer));
  CHECK(patchy::smart_object_lock_reason(*layer).empty());
  const auto placement = patchy::smart_object_placement_from_layer(*layer);
  CHECK(placement.has_value());
  // Pinned from the Photoshop 2026 capture: a 32x24 png placed 1:1 centered in 96x96.
  CHECK(placement->transform[0] == 32.0 && placement->transform[1] == 36.0);
  CHECK(placement->transform[4] == 64.0 && placement->transform[5] == 60.0);
  CHECK(placement->width == 32.0 && placement->height == 24.0);
  const auto* source = document.metadata().smart_objects.find(placement->uuid);
  CHECK(source != nullptr);
  CHECK(source->kind == patchy::SmartObjectSourceKind::Embedded);
  CHECK(source->filetype == "png ");
  CHECK(source->filename == "small.png");
  CHECK(source->file_bytes != nullptr);
  CHECK(source->file_bytes->size() == 8012);  // the original png, byte-for-byte
  CHECK(source->file_bytes->size() >= 8 && (*source->file_bytes)[1] == 'P' && (*source->file_bytes)[2] == 'N');
  CHECK(std::any_of(notices.begin(), notices.end(), [](const std::string& notice) {
    return notice.find("smart object") != std::string::npos;
  }));
}

void psd_smart_object_clean_resave_preserves_blocks_byte_identically() {
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-place-embedded-png.psd"));
  const auto resaved = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto sold_payload = [](const patchy::Document& doc) -> std::vector<std::uint8_t> {
    const auto* layer = find_layer_named(doc.layers(), "small");
    CHECK(layer != nullptr);
    for (const auto& block : layer->unknown_psd_blocks()) {
      if (block.key == "SoLd") {
        return block.payload;
      }
    }
    return {};
  };
  CHECK(!sold_payload(document).empty());
  CHECK(sold_payload(document) == sold_payload(resaved));  // untouched layers never regenerate
  const auto* original_source = document.metadata().smart_objects.find(
      patchy::smart_object_placement_from_layer(*find_layer_named(document.layers(), "small"))->uuid);
  const auto* resaved_source = resaved.metadata().smart_objects.find(
      patchy::smart_object_placement_from_layer(*find_layer_named(resaved.layers(), "small"))->uuid);
  CHECK(original_source != nullptr && resaved_source != nullptr);
  CHECK(*original_source->file_bytes == *resaved_source->file_bytes);
  CHECK(*original_source->original_element_bytes == *resaved_source->original_element_bytes);
}

void psd_smart_object_move_regenerates_placed_blocks() {
  auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-place-embedded-png.psd"));
  auto* layer = const_cast<patchy::Layer*>(find_layer_named(document.layers(), "small"));
  CHECK(layer != nullptr);
  const auto original_placement = patchy::smart_object_placement_from_layer(*layer);
  CHECK(original_placement.has_value());

  patchy::translate_moved_layer_metadata(*layer, 5, 3, document.width(), document.height());
  auto bounds = layer->bounds();
  bounds.x += 5;
  bounds.y += 3;
  layer->set_bounds(bounds);
  CHECK(patchy::layer_smart_object_block_dirty(*layer));

  const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto* reread_layer = find_layer_named(reread.layers(), "small");
  CHECK(reread_layer != nullptr);
  CHECK(patchy::layer_is_smart_object(*reread_layer));
  CHECK(patchy::smart_object_lock_reason(*reread_layer).empty());
  const auto reread_placement = patchy::smart_object_placement_from_layer(*reread_layer);
  CHECK(reread_placement.has_value());
  for (std::size_t i = 0; i < 8U; i += 2) {
    CHECK(reread_placement->transform[i] == original_placement->transform[i] + 5.0);
    CHECK(reread_placement->transform[i + 1] == original_placement->transform[i + 1] + 3.0);
  }
  // Patch-in-place: unmodeled descriptor keys (the ClMg OCIO conversion object) survive
  // regeneration, and the PlLd twin block was patched alongside SoLd.
  for (const auto& block : reread_layer->unknown_psd_blocks()) {
    if (block.key == "SoLd") {
      patchy::psd::BigEndianReader reader(block.payload);
      (void)patchy::psd::read_signature(reader);
      (void)reader.read_u32();
      (void)reader.read_u32();
      const auto descriptor = patchy::psd::read_descriptor(reader);
      CHECK(patchy::psd::descriptor_value(descriptor, "ClMg") != nullptr);
      CHECK(patchy::psd::descriptor_value(descriptor, "Crop") != nullptr);
    }
    if (block.key == "PlLd") {
      patchy::psd::BigEndianReader reader(block.payload);
      (void)patchy::psd::read_signature(reader);
      (void)reader.read_u32();
      const auto uuid_length = reader.read_u8();
      reader.skip(uuid_length);
      reader.skip(16);  // page, total pages, anti-alias, type
      CHECK(patchy::psd::read_f64(reader) == original_placement->transform[0] + 5.0);
      CHECK(patchy::psd::read_f64(reader) == original_placement->transform[1] + 3.0);
    }
  }
}

void smart_object_rescaled_placement_matches_photoshop_replace_rule() {
  // Pinned to the E5 COM captures (see AGENTS.md): the content-inch map and the quad
  // center are preserved and applied to the new content's pixel size and density.
  patchy::SmartObjectPlacement placement;
  placement.uuid = "old";
  placement.transform = {80.0, 85.0, 120.0, 85.0, 120.0, 115.0, 80.0, 115.0};
  placement.width = 40.0;
  placement.height = 30.0;
  placement.resolution = 72.0;

  const auto grown = patchy::rescaled_smart_object_placement(placement, 80.0, 60.0, 72.0);
  CHECK((grown.transform == std::array<double, 8>{60.0, 70.0, 140.0, 70.0, 140.0, 130.0, 60.0, 130.0}));
  CHECK(grown.width == 80.0 && grown.height == 60.0 && grown.resolution == 72.0);

  // A 300 dpi replacement shrinks by 72/300 (physical size preserved; Photoshop
  // additionally rounds the corners to whole pixels, Patchy keeps exact doubles).
  const auto dense = patchy::rescaled_smart_object_placement(placement, 80.0, 60.0, 300.0);
  CHECK(std::abs(dense.transform[0] - 90.4) < 1e-9);
  CHECK(std::abs(dense.transform[1] - 92.8) < 1e-9);
  CHECK(std::abs(dense.transform[4] - 109.6) < 1e-9);
  CHECK(std::abs(dense.transform[5] - 107.2) < 1e-9);
  CHECK(dense.resolution == 300.0);

  // A 50%-scaled placement keeps its scale factor about its own center (E5 case 3).
  patchy::SmartObjectPlacement scaled = placement;
  scaled.transform = {90.0, 93.0, 110.0, 93.0, 110.0, 108.0, 90.0, 108.0};
  const auto replaced = patchy::rescaled_smart_object_placement(scaled, 80.0, 60.0, 72.0);
  CHECK(std::abs(replaced.transform[0] - 80.0) < 1e-9);
  CHECK(std::abs(replaced.transform[1] - 85.5) < 1e-9);
  CHECK(std::abs(replaced.transform[4] - 120.0) < 1e-9);
  CHECK(std::abs(replaced.transform[5] - 115.5) < 1e-9);
}

void smart_object_store_remove_and_generated_uuid_shape() {
  patchy::SmartObjectStore store;
  const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(std::vector<std::uint8_t>{1, 2, 3});
  store.add_embedded("aaa", "a.png", "png ", bytes);
  store.add_embedded("bbb", "b.png", "png ", bytes);
  CHECK(store.find("aaa") != nullptr);
  CHECK(store.remove("aaa"));
  CHECK(store.find("aaa") == nullptr);
  CHECK(store.find("bbb") != nullptr);
  CHECK(!store.remove("aaa"));

  const auto uuid = patchy::generate_smart_object_uuid();
  CHECK(uuid.size() == 36U);
  for (std::size_t i = 0; i < uuid.size(); ++i) {
    if (i == 8U || i == 13U || i == 18U || i == 23U) {
      CHECK(uuid[i] == '-');
    } else {
      const bool hex = (uuid[i] >= '0' && uuid[i] <= '9') || (uuid[i] >= 'a' && uuid[i] <= 'f');
      CHECK(hex);
    }
  }
  CHECK(uuid != patchy::generate_smart_object_uuid());
}

void psd_smart_object_committed_psb_contents_round_trip() {
  auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-place-embedded-png.psd"));
  auto* layer = const_cast<patchy::Layer*>(find_layer_named(document.layers(), "small"));
  CHECK(layer != nullptr);
  const auto placement = patchy::smart_object_placement_from_layer(*layer);
  CHECK(placement.has_value());
  auto* source = document.metadata().smart_objects.find(placement->uuid);
  CHECK(source != nullptr);

  // Simulate an Edit Contents commit swapping the embedded png for PSB bytes (the
  // format Photoshop embeds for converted layers); the SoLd itself stays untouched.
  patchy::Document child(20, 10, patchy::PixelFormat::rgb8());
  child.add_pixel_layer("Contents", solid_rgb(20, 10, 10, 20, 30));
  const auto child_bytes = std::make_shared<const std::vector<std::uint8_t>>(
      patchy::psd::DocumentIo::write_layered_rgb8(child, patchy::psd::WriteOptions{true}));
  source->file_bytes = child_bytes;
  source->original_element_bytes = nullptr;
  source->dirty = true;

  const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto* reread_layer = find_layer_named(reread.layers(), "small");
  CHECK(reread_layer != nullptr);
  const auto reread_placement = patchy::smart_object_placement_from_layer(*reread_layer);
  CHECK(reread_placement.has_value());
  const auto* reread_source = reread.metadata().smart_objects.find(reread_placement->uuid);
  CHECK(reread_source != nullptr && reread_source->file_bytes != nullptr);
  CHECK(*reread_source->file_bytes == *child_bytes);  // dirty element re-embedded byte-identically
  const auto reread_child = patchy::psd::DocumentIo::read(
      {reread_source->file_bytes->data(), reread_source->file_bytes->size()});
  CHECK(reread_child.width() == 20 && reread_child.height() == 10);  // still opens as a PSB
}

void psd_descriptor_writer_round_trips_sold() {
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-place-embedded-png.psd"));
  const auto* layer = find_layer_named(document.layers(), "small");
  CHECK(layer != nullptr);
  check_sold_descriptor_round_trip(*layer);
}

void psd_descriptor_writer_round_trips_smart_filter_sold_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("ps2026_smart_filter.psd");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] ps2026_smart_filter fixture missing: " << path.string() << '\n';
    return;
  }
  const auto document = patchy::psd::DocumentIo::read_file(path);
  const auto* layer = find_layer_named(document.layers(), "Art copy");
  CHECK(layer != nullptr);
  check_sold_descriptor_round_trip(*layer);
}

void psb_write_accepts_over_30k_dimension_psd_rejects() {
  patchy::Document document(30001, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Wide", solid_rgb(30001, 1, 5, 6, 7));
  bool psd_threw = false;
  try {
    (void)patchy::psd::DocumentIo::write_layered_rgb8(document);
  } catch (const std::exception&) {
    psd_threw = true;
  }
  CHECK(psd_threw);  // over the 30k PSD cap: the writer must direct users to .psb

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document, patchy::psd::WriteOptions{true});
  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.width() == 30001);
  CHECK(read.layers().size() == 1);
  const auto* px = read.layers().front().pixels().pixel(30000, 0);
  CHECK(px[0] == 5 && px[1] == 6 && px[2] == 7);
}

void psd_layered_writer_bytes_are_stable() {
  // PSB support threads a large_document flag through every PSD length/RLE write
  // site; this FNV-1a pin proves the default PSD path emits the exact same bytes.
  // Re-pin only for deliberate format changes (the failure output prints the hash).
  patchy::Document document(8, 6, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(8, 6, 250, 240, 20));
  patchy::Layer top(document.allocate_layer_id(), "Top", solid_rgba(4, 3, 10, 20, 30, 200));
  top.set_bounds(patchy::Rect{2, 1, 4, 3});
  top.set_opacity(0.5F);
  top.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"fxrp", std::vector<std::uint8_t>(16, 3)});
  document.add_layer(std::move(top));
  document.metadata().unknown_psd_resources.push_back(
      patchy::UnknownPsdBlock{"lnk2", {9, 9, 9, 9, 9, 9, 9, 9}});
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  std::uint64_t hash = 1469598103934665603ULL;
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  constexpr std::uint64_t kExpected = 0xe7ed1d1689e2c94dULL;
  if (hash != kExpected) {
    std::cout << "psd layered writer hash: 0x" << std::hex << hash << std::dec << " size " << bytes.size() << "\n";
  }
  CHECK(hash == kExpected);
}

void psd_photoshop_unlinked_mask_fixture_reads_unlinked() {
  const auto document =
      patchy::psd::DocumentIo::read_file(patchy::test::committed_psd_fixture_path("photoshop-unlinked-mask.psd"));
  const auto* layer = find_layer_named(document.layers(), "Layer 1");
  CHECK(layer != nullptr);
  CHECK(layer->mask().has_value());
  CHECK(!patchy::layer_mask_linked(*layer));
}

constexpr std::array<std::uint8_t, 12> kLfx2UglgItem{0, 0, 0, 0, 'u', 'g', 'l', 'g', 'b', 'o', 'o', 'l'};

void psd_generated_drop_shadow_marks_angle_as_local() {
  patchy::Document document(4, 4, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer("Styled", solid_rgba(4, 4, 10, 20, 30, 255));
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.angle_degrees = 30.0F;
  layer.layer_style().drop_shadows.push_back(shadow);

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto payload = psd_layer_block_payload(psd_first_layer_extra_data(bytes), "lfx2");
  CHECK(payload.has_value());
  // 'uglg' must be false: Photoshop would otherwise ignore the stored angle and swing the
  // shadow to the document's global light direction.
  const auto found = std::search(payload->begin(), payload->end(), kLfx2UglgItem.begin(), kLfx2UglgItem.end());
  CHECK(found != payload->end());
  CHECK(*(found + kLfx2UglgItem.size()) == 0U);
}

void psd_drop_shadow_resolves_photoshop_global_light() {
  patchy::Document document(4, 4, patchy::PixelFormat::rgb8());
  // Global light angle resource (1037) holding -60 degrees.
  patchy::psd::BigEndianWriter resources;
  resources.write_u8('8');
  resources.write_u8('B');
  resources.write_u8('I');
  resources.write_u8('M');
  resources.write_u16(1037);
  resources.write_u16(0);  // empty pascal name, padded
  resources.write_u32(4);
  resources.write_u32(static_cast<std::uint32_t>(-60));
  document.metadata().raw_psd_image_resources = resources.bytes();

  auto& layer = document.add_pixel_layer("Styled", solid_rgba(4, 4, 10, 20, 30, 255));
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.angle_degrees = 120.0F;
  layer.layer_style().drop_shadows.push_back(shadow);

  auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  // Flip the written 'uglg' flag to true, simulating a Photoshop-authored
  // "use global light" shadow stored with a stale local angle.
  auto found = std::search(bytes.begin(), bytes.end(), kLfx2UglgItem.begin(), kLfx2UglgItem.end());
  CHECK(found != bytes.end());
  *(found + kLfx2UglgItem.size()) = 1U;

  auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 1);
  const auto& style = read.layers().front().layer_style();
  CHECK(style.drop_shadows.size() == 1);
  CHECK(std::lround(style.drop_shadows.front().angle_degrees) == -60);
  CHECK(!style.drop_shadows.front().use_global_light);

  // An untouched layer re-saves the preserved Photoshop lfx2 block byte-for-byte, which still
  // means -60 degrees to Photoshop via the raw global light resource. Once the style is edited
  // the preserved block is dropped, and the regenerated descriptor must carry the resolved
  // angle as a local one so Photoshop keeps rendering -60.
  std::erase_if(read.layers().front().unknown_psd_blocks(),
                [](const patchy::UnknownPsdBlock& block) { return block.key == "lfx2" || block.key == "lrFX"; });
  const auto resaved = patchy::psd::DocumentIo::write_layered_rgb8(read);
  const auto payload = psd_layer_block_payload(psd_first_layer_extra_data(resaved), "lfx2");
  CHECK(payload.has_value());
  const auto resaved_uglg =
      std::search(payload->begin(), payload->end(), kLfx2UglgItem.begin(), kLfx2UglgItem.end());
  CHECK(resaved_uglg != payload->end());
  CHECK(*(resaved_uglg + kLfx2UglgItem.size()) == 0U);
  constexpr std::array<std::uint8_t, 16> lagl_item{0, 0, 0,   0,   'l', 'a', 'g', 'l',
                                                   'U', 'n', 't', 'F', '#', 'A', 'n', 'g'};
  const auto resaved_lagl = std::search(payload->begin(), payload->end(), lagl_item.begin(), lagl_item.end());
  CHECK(resaved_lagl != payload->end());
  // IEEE-754 big-endian -60.0
  constexpr std::array<std::uint8_t, 8> minus_sixty{0xC0, 0x4E, 0, 0, 0, 0, 0, 0};
  CHECK(std::equal(minus_sixty.begin(), minus_sixty.end(), resaved_lagl + lagl_item.size()));
}

void psd_photoshop_global_light_shadow_fixture_resolves_angle() {
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-global-light-shadow.psd"));
  const auto* layer = find_layer_named(document.layers(), "Layer 1");
  CHECK(layer != nullptr);
  CHECK(layer->layer_style().drop_shadows.size() == 1);
  // The file stores lagl=30 with uglg=true and a document global light of 90; Photoshop
  // renders 90, so the import must too.
  CHECK(std::lround(layer->layer_style().drop_shadows.front().angle_degrees) == 90);
}

void psd_arrows_load_save_stays_compressed_if_available() {
  const auto path = arrows_fixture_path();
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] arrows PSD fixture missing: " << path.string() << '\n';
    return;
  }

  const auto source_size = std::filesystem::file_size(path);
  const auto document = patchy::psd::DocumentIo::read_file(path);
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto channels = psd_layer_channel_records(bytes);

  CHECK(std::any_of(channels.begin(), channels.end(),
                    [](const PsdLayerChannelRecord& channel) { return channel.compression == 1U; }));
  CHECK(bytes.size() < source_size * 5U);
}

void psd_layer_styles_round_trip_patchy_effects() {
  patchy::Document document(14, 14, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(14, 14, 255, 255, 255));
  patchy::Layer styled_layer(document.allocate_layer_id(), "Styled", solid_rgba(5, 5, 180, 40, 70, 255));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(patchy::Rect{4, 4, 5, 5});

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Multiply;
  shadow.color = patchy::RgbColor{10, 20, 30};
  shadow.opacity = 0.6F;
  shadow.angle_degrees = 135.0F;
  shadow.distance = 4.0F;
  shadow.spread = 15.0F;
  shadow.size = 6.0F;
  layer.layer_style().drop_shadows.push_back(shadow);

  patchy::LayerInnerShadow inner_shadow;
  inner_shadow.enabled = true;
  inner_shadow.blend_mode = patchy::BlendMode::Multiply;
  inner_shadow.color = patchy::RgbColor{8, 9, 10};
  inner_shadow.opacity = 0.7F;
  inner_shadow.angle_degrees = 120.0F;
  inner_shadow.distance = 2.0F;
  inner_shadow.choke = 20.0F;
  inner_shadow.size = 7.0F;
  layer.layer_style().inner_shadows.push_back(inner_shadow);
  auto second_inner_shadow = inner_shadow;
  second_inner_shadow.color = patchy::RgbColor{44, 45, 46};
  second_inner_shadow.distance = 0.0F;
  second_inner_shadow.size = 3.0F;
  layer.layer_style().inner_shadows.push_back(second_inner_shadow);

  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Screen;
  glow.color = patchy::RgbColor{250, 230, 80};
  glow.opacity = 0.5F;
  glow.spread = 25.0F;
  glow.size = 3.0F;
  layer.layer_style().outer_glows.push_back(glow);

  patchy::LayerInnerGlow inner_glow;
  inner_glow.enabled = true;
  inner_glow.blend_mode = patchy::BlendMode::Screen;
  inner_glow.color = patchy::RgbColor{240, 245, 210};
  inner_glow.opacity = 0.4F;
  inner_glow.choke = 10.0F;
  inner_glow.size = 4.0F;
  inner_glow.source = patchy::LayerInnerGlowSource::Edge;
  layer.layer_style().inner_glows.push_back(inner_glow);

  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = patchy::BlendMode::Normal;
  overlay.color = patchy::RgbColor{180, 30, 210};
  overlay.opacity = 0.85F;
  layer.layer_style().color_overlays.push_back(overlay);

  patchy::LayerGradientFill fill;
  fill.enabled = true;
  fill.blend_mode = patchy::BlendMode::Overlay;
  fill.opacity = 0.75F;
  fill.gradient.type = patchy::LayerStyleGradientType::Radial;
  fill.gradient.angle_degrees = 45.0F;
  fill.gradient.scale = 0.8F;
  fill.gradient.reverse = true;
  fill.gradient.color_stops.push_back(patchy::GradientColorStop{0.0F, patchy::RgbColor{20, 60, 240}});
  fill.gradient.color_stops.push_back(patchy::GradientColorStop{1.0F, patchy::RgbColor{20, 220, 80}});
  fill.gradient.alpha_stops.push_back(patchy::GradientAlphaStop{0.0F, 0.25F});
  fill.gradient.alpha_stops.push_back(patchy::GradientAlphaStop{1.0F, 1.0F});
  layer.layer_style().gradient_fills.push_back(fill);

  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = patchy::BlendMode::Normal;
  stroke.color = patchy::RgbColor{255, 220, 0};
  stroke.opacity = 0.9F;
  stroke.size = 2.0F;
  stroke.position = patchy::LayerStrokePosition::Inside;
  stroke.uses_gradient = true;
  stroke.gradient = fill.gradient;
  layer.layer_style().strokes.push_back(stroke);

  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.highlight_blend_mode = patchy::BlendMode::Screen;
  bevel.highlight_color = patchy::RgbColor{255, 250, 220};
  bevel.highlight_opacity = 0.7F;
  bevel.shadow_blend_mode = patchy::BlendMode::Multiply;
  bevel.shadow_color = patchy::RgbColor{20, 15, 10};
  bevel.shadow_opacity = 0.65F;
  bevel.angle_degrees = 100.0F;
  bevel.altitude_degrees = 35.0F;
  bevel.depth = 1.5F;
  bevel.size = 4.0F;
  bevel.direction_up = false;
  layer.layer_style().bevels.push_back(bevel);

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto extra_data = psd_layer_extra_data(bytes, 1);
  const auto lfx2_payload = psd_layer_block_payload(extra_data, "lfx2");
  CHECK(lfx2_payload.has_value());
  CHECK(!psd_layer_block_payload(extra_data, "plFX").has_value());
  // Photoshop 2026 only resolves 'BlnM' enum values written as full stringIDs
  // ("overlay"); the 4-char codes ('Ovrl') are silently read as Normal.
  const std::string lfx2_text(lfx2_payload->begin(), lfx2_payload->end());
  CHECK(lfx2_text.find("overlay") != std::string::npos);
  CHECK(lfx2_text.find("Ovrl") == std::string::npos);
  // Photoshop also resets a GrFl blend mode unless the descriptor carries its
  // own present/showInDialog shape, so the writer mirrors it exactly.
  CHECK(lfx2_text.find("showInDialog") != std::string::npos);
  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2);
  const auto& style = read.layers()[1].layer_style();
  CHECK(!style.empty());
  CHECK(style.drop_shadows.size() == 1);
  CHECK(style.drop_shadows.front().blend_mode == patchy::BlendMode::Multiply);
  CHECK(style.drop_shadows.front().color.red == 10);
  CHECK(style.drop_shadows.front().opacity == 0.6F);
  CHECK(style.inner_shadows.size() == 2);
  CHECK(style.inner_shadows.front().color.blue == 10);
  CHECK(style.inner_shadows.front().choke == 20.0F);
  CHECK(style.inner_shadows[1].color.red == 44);
  CHECK(style.inner_shadows[1].distance == 0.0F);
  CHECK(style.outer_glows.size() == 1);
  CHECK(style.outer_glows.front().color.green == 230);
  CHECK(style.inner_glows.size() == 1);
  CHECK(style.inner_glows.front().color.red == 240);
  CHECK(style.inner_glows.front().source == patchy::LayerInnerGlowSource::Edge);
  CHECK(style.color_overlays.size() == 1);
  CHECK(style.color_overlays.front().color.blue == 210);
  CHECK(style.color_overlays.front().opacity == 0.85F);
  CHECK(style.gradient_fills.size() == 1);
  CHECK(style.gradient_fills.front().blend_mode == patchy::BlendMode::Overlay);
  CHECK(style.gradient_fills.front().gradient.type == patchy::LayerStyleGradientType::Radial);
  CHECK(style.gradient_fills.front().gradient.reverse);
  CHECK(style.gradient_fills.front().gradient.alpha_stops.size() == 2);
  CHECK(style.strokes.size() == 1);
  CHECK(style.strokes.front().position == patchy::LayerStrokePosition::Inside);
  CHECK(style.strokes.front().uses_gradient);
  CHECK(style.bevels.size() == 1);
  CHECK(style.bevels.front().shadow_color.blue == 10);
  CHECK(!style.bevels.front().direction_up);
}

void psd_writer_uses_preserved_photoshop_style_blocks_without_private_duplicates() {
  patchy::Document document(3, 3, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer("Photoshop Style", solid_rgba(3, 3, 120, 80, 40, 255));

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.color = patchy::RgbColor{10, 20, 30};
  shadow.opacity = 0.5F;
  layer.layer_style().drop_shadows.push_back(shadow);
  const std::vector<std::uint8_t> photoshop_style_payload{1, 2, 3, 4};
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"lfx2", photoshop_style_payload});

  const auto extra_data = psd_first_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(document));
  CHECK(psd_layer_block_payload(extra_data, "lfx2").value() == photoshop_style_payload);
  CHECK(!psd_layer_block_payload(extra_data, "plFX").has_value());
}

void psd_arrows_imports_photoshop_inner_effects() {
  const auto path = arrows_fixture_path();
  CHECK(std::filesystem::exists(path));

  const auto document = patchy::psd::DocumentIo::read_file(path);
  const auto* layer = find_layer_named(document.layers(), "Layer 3 copy");
  CHECK(layer != nullptr);
  CHECK(layer_has_psd_block(*layer, "lfx2"));
  CHECK(layer_has_psd_block(*layer, "lrFX"));

  const auto* inner_shadow = first_enabled_inner_shadow(*layer);
  CHECK(inner_shadow != nullptr);
  CHECK(inner_shadow->blend_mode == patchy::BlendMode::Multiply);
  CHECK(inner_shadow->color.red == 0);
  CHECK(close_float(inner_shadow->opacity, 0.75F));
  CHECK(close_float(inner_shadow->distance, 0.0F));
  CHECK(close_float(inner_shadow->size, 24.0F));

  const auto* inner_glow = first_enabled_inner_glow(*layer);
  CHECK(inner_glow != nullptr);
  CHECK(inner_glow->blend_mode == patchy::BlendMode::Screen);
  CHECK(inner_glow->color.red == 255);
  CHECK(inner_glow->color.green == 255);
  CHECK(inner_glow->color.blue == 190);
  CHECK(close_float(inner_glow->opacity, 0.75F));
  CHECK(close_float(inner_glow->size, 5.0F));
  CHECK(inner_glow->source == patchy::LayerInnerGlowSource::Edge);

  CHECK(layer->layer_style().outer_glows.size() == 1);

  const auto* shape = find_layer_named(document.layers(), "Shape 1");
  CHECK(shape != nullptr);
  CHECK(first_enabled_drop_shadow(*shape) != nullptr);
  CHECK(!shape->layer_style().gradient_fills.empty());
  CHECK(!shape->layer_style().strokes.empty());

  const auto round_tripped =
      patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto* round_tripped_layer = find_layer_named(round_tripped.layers(), "Layer 3 copy");
  CHECK(round_tripped_layer != nullptr);
  CHECK(layer_has_psd_block(*round_tripped_layer, "lfx2"));
  CHECK(layer_has_psd_block(*round_tripped_layer, "lrFX"));
  CHECK(first_enabled_inner_shadow(*round_tripped_layer) != nullptr);
  CHECK(first_enabled_inner_glow(*round_tripped_layer) != nullptr);
}

void compositor_renders_drop_shadow_spread() {
  auto make_document = [](float spread) {
    patchy::Document document(56, 48, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(56, 48, 0, 0, 0));
    patchy::Layer layer(document.allocate_layer_id(), "Source", solid_rgba(4, 4, 0, 0, 0, 255));
    auto& source = document.add_layer(std::move(layer));
    source.set_bounds(patchy::Rect{26, 22, 4, 4});

    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.blend_mode = patchy::BlendMode::Normal;
    shadow.color = patchy::RgbColor{255, 255, 255};
    shadow.opacity = 1.0F;
    shadow.angle_degrees = 90.0F;
    shadow.distance = 0.0F;
    shadow.size = 8.0F;
    shadow.spread = spread;
    source.layer_style().drop_shadows.push_back(shadow);
    return document;
  };

  const auto no_spread = patchy::Compositor{}.flatten_rgb8(make_document(0.0F));
  const auto spread = patchy::Compositor{}.flatten_rgb8(make_document(100.0F));
  const auto* no_spread_px = no_spread.pixel(18, 23);
  const auto* spread_px = spread.pixel(18, 23);
  CHECK(spread_px[0] > 15);
  CHECK(spread_px[0] > no_spread_px[0] + 10);
}

void compositor_drop_shadow_full_spread_keeps_rounded_support() {
  // qual_rca_pinout.psd's white label plates: spread 100, size 21. Spread must expand
  // the matte with rounded Euclidean corners (Photoshop semantics, COM-probed); the
  // old post-blur gain binarized the box blur's tail, so the plate was the kernel's
  // rectangular support: per-glyph boxes jutting out to size * sqrt(2) on diagonals.
  patchy::Document document(160, 160, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(160, 160, 0, 0, 0));
  patchy::Layer layer(document.allocate_layer_id(), "Source", solid_rgba(40, 40, 10, 10, 10, 255));
  auto& source = document.add_layer(std::move(layer));
  source.set_bounds(patchy::Rect{60, 60, 40, 40});

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Normal;
  shadow.color = patchy::RgbColor{255, 255, 255};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 90.0F;
  shadow.distance = 0.0F;
  shadow.spread = 100.0F;
  shadow.size = 21.0F;
  source.layer_style().drop_shadows.push_back(shadow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto euclidean_distance_from_source = [](std::int32_t x, std::int32_t y) {
    const auto dx = static_cast<float>(x < 60 ? 60 - x : (x > 99 ? x - 99 : 0));
    const auto dy = static_cast<float>(y < 60 ? 60 - y : (y > 99 ? y - 99 : 0));
    return std::sqrt(dx * dx + dy * dy);
  };

  // The plate must stay solid out to nearly the full size along the axes AND the
  // diagonals (rounded expansion), and die out within ~1px past it everywhere: any
  // shadow beyond that is a rectangular corner chunk (or float dust) leaking through.
  CHECK(flattened.pixel(118, 80)[0] >= 250);   // axis, 19px out
  CHECK(flattened.pixel(112, 112)[0] >= 250);  // diagonal, 18.4px out
  int painted_beyond_falloff = 0;
  for (std::int32_t y = 0; y < flattened.height(); ++y) {
    for (std::int32_t x = 0; x < flattened.width(); ++x) {
      if (euclidean_distance_from_source(x, y) > 23.0F && flattened.pixel(x, y)[0] > 8) {
        ++painted_beyond_falloff;
      }
    }
  }
  CHECK(painted_beyond_falloff == 0);
}

void compositor_renders_drop_shadow_beyond_outer_glow() {
  patchy::Document document(96, 80, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(96, 80, 232, 224, 204));

  patchy::Layer layer(document.allocate_layer_id(), "Styled", solid_rgba(24, 10, 255, 255, 255, 255));
  auto& source = document.add_layer(std::move(layer));
  source.set_bounds(patchy::Rect{34, 18, 24, 10});

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Multiply;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 90.0F;
  shadow.distance = 3.0F;
  shadow.size = 30.0F;
  shadow.spread = 74.0F;
  source.layer_style().drop_shadows.push_back(shadow);

  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{184, 81, 74};
  glow.opacity = 1.0F;
  glow.size = 18.0F;
  glow.spread = 100.0F;
  source.layer_style().outer_glows.push_back(glow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* shadow_px = flattened.pixel(46, 48);
  CHECK(shadow_px[0] < 220);
  CHECK(shadow_px[1] < 214);
  CHECK(shadow_px[2] < 196);
}

void compositor_renders_inner_shadow() {
  patchy::Document document(40, 40, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(40, 40, 255, 255, 255));
  patchy::Layer layer(document.allocate_layer_id(), "Source", solid_rgba(20, 20, 255, 255, 255, 255));
  auto& source = document.add_layer(std::move(layer));
  source.set_bounds(patchy::Rect{10, 10, 20, 20});

  patchy::LayerInnerShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Normal;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.distance = 0.0F;
  shadow.size = 8.0F;
  source.layer_style().inner_shadows.push_back(shadow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(11, 11)[0] < flattened.pixel(20, 20)[0]);
  CHECK(flattened.pixel(20, 20)[0] > 220);
}

void compositor_renders_inner_glow() {
  patchy::Document document(40, 40, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(40, 40, 0, 0, 0));
  patchy::Layer layer(document.allocate_layer_id(), "Source", solid_rgba(20, 20, 0, 0, 0, 255));
  auto& source = document.add_layer(std::move(layer));
  source.set_bounds(patchy::Rect{10, 10, 20, 20});

  patchy::LayerInnerGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{255, 255, 255};
  glow.opacity = 1.0F;
  glow.size = 8.0F;
  glow.source = patchy::LayerInnerGlowSource::Edge;
  source.layer_style().inner_glows.push_back(glow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(11, 11)[0] > flattened.pixel(20, 20)[0] + 20);
  CHECK(flattened.pixel(20, 20)[0] < 20);
}

patchy::PixelBuffer choke_probe_square_with_hole(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  // 120x120 solid square with an 8x8 transparent hole at local 56..63 (document
  // 96..103 once placed at 40,40): the hole is the shape whose interior falloff
  // exposes a non-Euclidean choke support.
  auto pixels = solid_rgba(120, 120, r, g, b, 255);
  for (std::int32_t y = 56; y < 64; ++y) {
    for (std::int32_t x = 56; x < 64; ++x) {
      pixels.pixel(x, y)[3] = 0;
    }
  }
  return pixels;
}

float choke_probe_distance_from_hole(std::int32_t x, std::int32_t y) {
  const auto dx = static_cast<float>(x < 96 ? 96 - x : (x > 103 ? x - 103 : 0));
  const auto dy = static_cast<float>(y < 96 ? 96 - y : (y > 103 ? y - 103 : 0));
  return std::sqrt(dx * dx + dy * dy);
}

void compositor_inner_shadow_full_choke_keeps_rounded_interior() {
  // Photoshop's Choke is the interior mirror of the drop-shadow Spread (COM-probed
  // July 2026 with choke 0/50/100 renders): the inverse matte expands with rounded
  // Euclidean corners to choke% x size and only the remaining (1 - choke%) x size
  // is blurred. The old post-blur gain ((1 - blur) / (1 - choke)) instead amplified
  // the box blur's square-support tail: a small transparent hole radiated a
  // ~1.5 x size rounded box of half-tone dust rather than a size-radius disc.
  patchy::Document document(200, 200, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(200, 200, 255, 255, 255));
  patchy::Layer layer(document.allocate_layer_id(), "Source", choke_probe_square_with_hole(255, 255, 255));
  auto& source = document.add_layer(std::move(layer));
  source.set_bounds(patchy::Rect{40, 40, 120, 120});

  patchy::LayerInnerShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Normal;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 120.0F;
  shadow.distance = 0.0F;
  shadow.choke = 100.0F;
  shadow.size = 21.0F;
  source.layer_style().inner_shadows.push_back(shadow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  // Solid out to the full size along the axis AND the diagonal from the hole
  // (Photoshop renders exactly these pixels solid), and along the straight edge.
  CHECK(flattened.pixel(124, 99)[0] <= 5);   // axis, 21st pixel right of the hole
  CHECK(flattened.pixel(117, 117)[0] <= 5);  // diagonal, 19.8px from the hole corner
  CHECK(flattened.pixel(59, 80)[0] <= 5);    // straight edge band, 20th pixel deep
  CHECK(flattened.pixel(64, 80)[0] >= 250);  // straight edge band ends at size
  // Hard bound past the Euclidean support: interior pixels farther than size + 2.5
  // from both the hole and the outer contour must stay clean white.
  int painted_beyond_falloff = 0;
  for (std::int32_t y = 40; y < 160; ++y) {
    for (std::int32_t x = 40; x < 160; ++x) {
      const auto interior_depth = std::min(std::min(x - 40, y - 40), std::min(159 - x, 159 - y));
      if (interior_depth > 23 && choke_probe_distance_from_hole(x, y) > 23.5F && flattened.pixel(x, y)[0] < 247) {
        ++painted_beyond_falloff;
      }
    }
  }
  CHECK(painted_beyond_falloff == 0);
}

void compositor_inner_glow_full_choke_keeps_rounded_interior() {
  // Inner glow's Edge-source choke shares the inner-shadow pipeline; same geometry
  // and Photoshop-probed expectations with the colors flipped.
  patchy::Document document(200, 200, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(200, 200, 0, 0, 0));
  patchy::Layer layer(document.allocate_layer_id(), "Source", choke_probe_square_with_hole(0, 0, 0));
  auto& source = document.add_layer(std::move(layer));
  source.set_bounds(patchy::Rect{40, 40, 120, 120});

  patchy::LayerInnerGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{255, 255, 255};
  glow.opacity = 1.0F;
  glow.choke = 100.0F;
  glow.size = 21.0F;
  glow.source = patchy::LayerInnerGlowSource::Edge;
  source.layer_style().inner_glows.push_back(glow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(124, 99)[0] >= 250);   // axis, 21st pixel right of the hole
  CHECK(flattened.pixel(117, 117)[0] >= 250);  // diagonal, 19.8px from the hole corner
  CHECK(flattened.pixel(59, 80)[0] >= 250);    // straight edge band, 20th pixel deep
  CHECK(flattened.pixel(64, 80)[0] <= 5);      // straight edge band ends at size
  int painted_beyond_falloff = 0;
  for (std::int32_t y = 40; y < 160; ++y) {
    for (std::int32_t x = 40; x < 160; ++x) {
      const auto interior_depth = std::min(std::min(x - 40, y - 40), std::min(159 - x, 159 - y));
      if (interior_depth > 23 && choke_probe_distance_from_hole(x, y) > 23.5F && flattened.pixel(x, y)[0] > 8) {
        ++painted_beyond_falloff;
      }
    }
  }
  CHECK(painted_beyond_falloff == 0);
}

void compositor_inner_glow_center_choke_erodes_matte_geometrically() {
  // Center-source choke erodes the matte geometrically (COM-probed: choke 100 pulls
  // the glow back to a hard Euclidean erosion by the full size, dark band outside
  // it); the old code ignored choke for the Center source entirely.
  patchy::Document document(200, 200, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(200, 200, 0, 0, 0));
  patchy::Layer layer(document.allocate_layer_id(), "Source", solid_rgba(120, 120, 0, 0, 0, 255));
  auto& source = document.add_layer(std::move(layer));
  source.set_bounds(patchy::Rect{40, 40, 120, 120});

  patchy::LayerInnerGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{255, 255, 255};
  glow.opacity = 1.0F;
  glow.choke = 100.0F;
  glow.size = 21.0F;
  glow.source = patchy::LayerInnerGlowSource::Center;
  source.layer_style().inner_glows.push_back(glow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(50, 99)[0] <= 5);    // depth 10: inside the choked band, no glow
  CHECK(flattened.pixel(60, 99)[0] <= 5);    // depth 20: still inside the band
  CHECK(flattened.pixel(62, 99)[0] >= 250);  // depth 22: the eroded core lights up
  CHECK(flattened.pixel(99, 99)[0] >= 250);  // deep center stays lit
}

void psd_qual_rca_pinout_imports_white_drop_shadows() {
  const auto path = qual_rca_pinout_fixture_path();
  CHECK(std::filesystem::exists(path));

  const auto document = patchy::psd::DocumentIo::read_file(path);
  const std::vector<std::string> label_names = {
      "1=G",
      "10=G",
      "9=Video",
      "4=Audio (R)",
      "5=Audio (W)",
  };
  for (const auto& name : label_names) {
    const auto* layer = find_layer_named(document.layers(), name);
    CHECK(layer != nullptr);
    const auto* shadow = first_enabled_drop_shadow(*layer);
    CHECK(shadow != nullptr);
    CHECK(shadow->blend_mode == patchy::BlendMode::Normal);
    CHECK(shadow->color.red == 255);
    CHECK(shadow->color.green == 255);
    CHECK(shadow->color.blue == 255);
    CHECK(close_float(shadow->opacity, 1.0F));
    CHECK(close_float(shadow->angle_degrees, 90.0F));
    CHECK(close_float(shadow->distance, 1.0F));
    CHECK(close_float(shadow->spread, 100.0F));
    CHECK(close_float(shadow->size, 21.0F));
  }
}

void psd_qual_rca_pinout_point_text_imports_as_point_text() {
  const auto path = qual_rca_pinout_fixture_path();
  CHECK(std::filesystem::exists(path));

  const auto document = patchy::psd::DocumentIo::read_file(path);
  const std::vector<std::string> point_text_layers = {
      "1=G",
      "10=G",
      "9=Video",
      "4=Audio (R)",
      "5=Audio (W)",
      "12345678910",
  };
  for (const auto& name : point_text_layers) {
    const auto* layer = find_layer_named(document.layers(), name);
    CHECK(layer != nullptr);
    CHECK(layer->metadata().at(patchy::kLayerMetadataTextFlow) == "point");
    CHECK(layer->metadata().at(patchy::kLayerMetadataTextSourceBlock) == "TySh");
    CHECK(layer->metadata().contains(patchy::kLayerMetadataTextTransform));
    CHECK(layer->metadata().contains(patchy::kLayerMetadataPsdTextTransform));
    CHECK(layer->metadata().contains(patchy::kLayerMetadataPsdTextBoundingBox));
    CHECK(std::stoi(layer->metadata().at(patchy::kLayerMetadataTextBoxWidth)) == layer->bounds().width);
    CHECK(std::stoi(layer->metadata().at(patchy::kLayerMetadataTextBoxHeight)) == layer->bounds().height);
  }
}

void psd_qual_rca_pinout_round_trips_styles_and_text_metadata() {
  const auto path = qual_rca_pinout_fixture_path();
  CHECK(std::filesystem::exists(path));

  const auto document = patchy::psd::DocumentIo::read_file(path);
  const auto round_tripped =
      patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto* layer = find_layer_named(round_tripped.layers(), "5=Audio (W)");
  CHECK(layer != nullptr);
  CHECK(layer_has_psd_block(*layer, "lfx2"));
  CHECK(layer_has_psd_block(*layer, "lrFX"));
  CHECK(layer_has_psd_block(*layer, "TySh"));
  CHECK(!layer_has_psd_block(*layer, "plFX"));
  CHECK(layer->metadata().at(patchy::kLayerMetadataTextFlow) == "point");
  CHECK(layer->metadata().at(patchy::kLayerMetadataTextSourceBlock) == "TySh");
  const auto* shadow = first_enabled_drop_shadow(*layer);
  CHECK(shadow != nullptr);
  CHECK(shadow->blend_mode == patchy::BlendMode::Normal);
  CHECK(shadow->color.red == 255);
  CHECK(shadow->color.green == 255);
  CHECK(shadow->color.blue == 255);
  CHECK(close_float(shadow->opacity, 1.0F));
  CHECK(close_float(shadow->spread, 100.0F));
  CHECK(close_float(shadow->size, 21.0F));
}

void psd_qual_rca_pinout_writes_comparison_artifacts() {
  const auto path = qual_rca_pinout_fixture_path();
  CHECK(std::filesystem::exists(path));

  patchy::psd::ReadOptions flat_options;
  flat_options.prefer_flat_composite = true;
  const auto photoshop_reference = patchy::psd::DocumentIo::read_file(path, flat_options);
  const auto editable_document = patchy::psd::DocumentIo::read_file(path);
  const auto reference_flat = patchy::Compositor{}.flatten_rgb8(photoshop_reference);
  const auto patchy_flat = patchy::Compositor{}.flatten_rgb8(editable_document);
  CHECK(reference_flat.width() == patchy_flat.width());
  CHECK(reference_flat.height() == patchy_flat.height());

  const auto diff = rgb_diff_image(reference_flat, patchy_flat);
  const auto metrics = rgb_diff_metrics(reference_flat, patchy_flat);
  write_rgb8_bmp_artifact("psd_qual_rca_pinout_photoshop_composite", reference_flat);
  write_rgb8_bmp_artifact("psd_qual_rca_pinout_patchy_composite", patchy_flat);
  write_rgb8_bmp_artifact("psd_qual_rca_pinout_diff", diff);
  write_qual_rca_pinout_report(metrics, editable_document);

  CHECK(metrics.pixels == static_cast<std::uint64_t>(reference_flat.width()) *
                              static_cast<std::uint64_t>(reference_flat.height()));
  CHECK(std::filesystem::exists(std::filesystem::path("test-artifacts") /
                                "psd_qual_rca_pinout_compatibility_report.txt"));
  CHECK(std::filesystem::exists(std::filesystem::path("test-artifacts") /
                                "psd_qual_rca_pinout_compatibility_report.json"));
}

void psd_checkbox_bevel_emboss_writes_comparison_artifacts_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("checkbox.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  patchy::psd::ReadOptions flat_options;
  flat_options.prefer_flat_composite = true;
  const auto photoshop_reference = patchy::psd::DocumentIo::read_file(path, flat_options);
  const auto editable_document = patchy::psd::DocumentIo::read_file(path);
  const auto reference_flat = patchy::Compositor{}.flatten_rgb8(photoshop_reference);
  const auto patchy_flat = patchy::Compositor{}.flatten_rgb8(editable_document);
  CHECK(reference_flat.width() == patchy_flat.width());
  CHECK(reference_flat.height() == patchy_flat.height());

  int bevel_layers = 0;
  std::function<void(const std::vector<patchy::Layer>&)> visit_layers = [&](const std::vector<patchy::Layer>& layers) {
    for (const auto& layer : layers) {
      if (!layer.layer_style().bevels.empty()) {
        ++bevel_layers;
      }
      visit_layers(layer.children());
    }
  };
  visit_layers(editable_document.layers());
  CHECK(bevel_layers >= 1);

  const auto diff = rgb_diff_image(reference_flat, patchy_flat);
  const auto metrics = rgb_diff_metrics(reference_flat, patchy_flat);
  write_rgb8_bmp_artifact("psd_checkbox_photoshop_composite", reference_flat);
  write_rgb8_bmp_artifact("psd_checkbox_patchy_composite", patchy_flat);
  write_rgb8_bmp_artifact("psd_checkbox_diff", diff);

  std::filesystem::create_directories("test-artifacts");
  std::ofstream report(std::filesystem::path("test-artifacts") / "psd_checkbox_compatibility_report.txt");
  report << "PSD compatibility comparison: checkbox.psd\n";
  report << "pixels: " << metrics.pixels << "\n";
  report << "differing_pixels: " << metrics.differing_pixels << "\n";
  report << "mean_abs_channel_delta: " << std::fixed << std::setprecision(3) << metrics.mean_abs_channel_delta
         << "\n";
  report << "max_channel_delta: " << metrics.max_channel_delta << "\n";
  report << "bevel_layers: " << bevel_layers << "\n";
}

void psd_adjustment_layers_render_and_round_trip() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(2, 2, 120, 40, 40));

  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::ColorBalance;
  settings.color_balance = patchy::ColorBalanceAdjustment{50, 0, 0};
  patchy::Layer adjustment(document.allocate_layer_id(), "Warmth", patchy::LayerKind::Adjustment);
  adjustment.set_bounds(patchy::Rect::from_size(document.width(), document.height()));
  patchy::configure_adjustment_layer(adjustment, settings);
  document.add_layer(std::move(adjustment));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(0, 0)[0] > 240);
  CHECK(flattened.pixel(0, 0)[1] == 40);

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  auto round_tripped = patchy::psd::DocumentIo::read(bytes);
  CHECK(round_tripped.layers().size() == 2);
  CHECK(round_tripped.layers().back().kind() == patchy::LayerKind::Adjustment);
  const auto round_tripped_settings = patchy::adjustment_settings_from_layer(round_tripped.layers().back());
  CHECK(round_tripped_settings.has_value());
  CHECK(round_tripped_settings->kind == patchy::AdjustmentKind::ColorBalance);
  CHECK(round_tripped_settings->color_balance.cyan_red == 50);
  const auto round_tripped_flattened = patchy::Compositor{}.flatten_rgb8(round_tripped);
  CHECK(round_tripped_flattened.pixel(0, 0)[0] == flattened.pixel(0, 0)[0]);
  CHECK(round_tripped_flattened.pixel(0, 0)[1] == flattened.pixel(0, 0)[1]);
}

void psd_levels_adjustment_channel_round_trips() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(1, 1, 0, 200, 0));

  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Levels;
  settings.levels.channel = patchy::LevelsChannel::Red;
  settings.levels.red.black_output = 255;
  patchy::Layer adjustment(document.allocate_layer_id(), "Red Levels", patchy::LayerKind::Adjustment);
  adjustment.set_bounds(patchy::Rect::from_size(document.width(), document.height()));
  patchy::configure_adjustment_layer(adjustment, settings);
  document.add_layer(std::move(adjustment));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(0, 0)[0] == 255);
  CHECK(flattened.pixel(0, 0)[1] == 200);
  CHECK(flattened.pixel(0, 0)[2] == 0);

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto round_tripped = patchy::psd::DocumentIo::read(bytes);
  CHECK(round_tripped.layers().size() == 2);
  const auto round_tripped_settings = patchy::adjustment_settings_from_layer(round_tripped.layers().back());
  CHECK(round_tripped_settings.has_value());
  CHECK(round_tripped_settings->kind == patchy::AdjustmentKind::Levels);
  CHECK(round_tripped_settings->levels.red.black_output == 255);
  CHECK(round_tripped_settings->levels.channel == patchy::LevelsChannel::Red);
  const auto round_tripped_flattened = patchy::Compositor{}.flatten_rgb8(round_tripped);
  CHECK(round_tripped_flattened.pixel(0, 0)[0] == 255);
  CHECK(round_tripped_flattened.pixel(0, 0)[1] == 200);
  CHECK(round_tripped_flattened.pixel(0, 0)[2] == 0);
}

void psd_levels_adjustment_writes_native_levl_and_patchy_fallback() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(1, 1, 32, 128, 220));

  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Levels;
  settings.levels = patchy::LevelsAdjustment{12, 240, 125, 4, 250};
  settings.levels.red = patchy::LevelsRecord{5, 230, 90, 11, 244};
  settings.levels.green = patchy::LevelsRecord{9, 220, 110, 13, 242};
  settings.levels.blue = patchy::LevelsRecord{17, 210, 140, 19, 238};
  patchy::Layer adjustment(document.allocate_layer_id(), "Native Levels", patchy::LayerKind::Adjustment);
  adjustment.set_bounds(patchy::Rect::from_size(document.width(), document.height()));
  patchy::configure_adjustment_layer(adjustment, settings);
  document.add_layer(std::move(adjustment));

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto extra_data = psd_layer_extra_data(bytes, 1);
  const auto levl = psd_layer_block_payload(extra_data, "levl");
  CHECK(levl.has_value());
  CHECK(psd_layer_block_payload(extra_data, "plAD").has_value());
  CHECK(levl->size() == 2U + 29U * 10U);
  CHECK(psd_levels_payload_record(*levl, 0).black_input == 12);
  CHECK(psd_levels_payload_record(*levl, 0).gamma_percent == 125);
  CHECK(psd_levels_payload_record(*levl, 1).black_output == 11);
  CHECK(psd_levels_payload_record(*levl, 2).white_input == 220);
  CHECK(psd_levels_payload_record(*levl, 3).white_output == 238);
  CHECK(psd_levels_payload_record(*levl, 4).black_input == 0);
  CHECK(psd_levels_payload_record(*levl, 4).white_input == 255);
  CHECK(psd_levels_payload_record(*levl, 4).black_output == 0);
  CHECK(psd_levels_payload_record(*levl, 4).white_output == 255);
  CHECK(psd_levels_payload_record(*levl, 4).gamma_percent == 100);
}

void psd_native_levels_adjustment_imports_without_patchy_block() {
  std::array<patchy::LevelsRecord, 4> records{};
  records[1].black_output = 255;
  const auto bytes = single_adjustment_layer_psd({{{'l', 'e', 'v', 'l'}, test_photoshop_levels_payload(records)}});
  const auto document = patchy::psd::DocumentIo::read(bytes);
  CHECK(document.layers().size() == 1);
  CHECK(document.layers().front().kind() == patchy::LayerKind::Adjustment);
  const auto settings = patchy::adjustment_settings_from_layer(document.layers().front());
  CHECK(settings.has_value());
  CHECK(settings->kind == patchy::AdjustmentKind::Levels);
  CHECK(settings->levels.red.black_output == 255);

  patchy::Document composited(1, 1, patchy::PixelFormat::rgb8());
  composited.add_pixel_layer("Base", solid_rgb(1, 1, 0, 200, 0));
  patchy::Layer adjustment(composited.allocate_layer_id(), "Native Levels", patchy::LayerKind::Adjustment);
  patchy::configure_adjustment_layer(adjustment, *settings);
  composited.add_layer(std::move(adjustment));
  const auto flattened = patchy::Compositor{}.flatten_rgb8(composited);
  CHECK(flattened.pixel(0, 0)[0] == 255);
  CHECK(flattened.pixel(0, 0)[1] == 200);
  CHECK(flattened.pixel(0, 0)[2] == 0);
}

void psd_native_levels_overrides_stale_patchy_fallback() {
  patchy::Document stale_document(1, 1, patchy::PixelFormat::rgb8());
  stale_document.add_pixel_layer("Base", solid_rgb(1, 1, 0, 200, 0));
  patchy::AdjustmentSettings stale_settings;
  stale_settings.kind = patchy::AdjustmentKind::Levels;
  stale_settings.levels.blue.black_output = 255;
  patchy::Layer stale_adjustment(stale_document.allocate_layer_id(), "Stale Levels", patchy::LayerKind::Adjustment);
  stale_adjustment.set_bounds(patchy::Rect::from_size(1, 1));
  patchy::configure_adjustment_layer(stale_adjustment, stale_settings);
  stale_document.add_layer(std::move(stale_adjustment));
  const auto stale_extra = psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(stale_document), 1);
  const auto stale_plad = psd_layer_block_payload(stale_extra, "plAD");
  CHECK(stale_plad.has_value());

  std::array<patchy::LevelsRecord, 4> native_records{};
  native_records[1].black_output = 255;
  const auto bytes = single_adjustment_layer_psd({{{'l', 'e', 'v', 'l'}, test_photoshop_levels_payload(native_records)},
                                                  {{'p', 'l', 'A', 'D'}, *stale_plad}});
  const auto document = patchy::psd::DocumentIo::read(bytes);
  const auto settings = patchy::adjustment_settings_from_layer(document.layers().front());
  CHECK(settings.has_value());
  CHECK(settings->levels.red.black_output == 255);
  CHECK(settings->levels.blue.black_output == 0);
}

void psd_writer_uses_photoshop_bottom_to_top_layer_record_order() {
  patchy::Document document(3, 2, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(3, 2, 255, 255, 255));
  document.add_pixel_layer("Middle", solid_rgba(3, 2, 80, 120, 180, 255));
  document.add_pixel_layer("Top", solid_rgba(3, 2, 220, 20, 60, 192));

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto names = psd_raw_layer_record_names(bytes);

  CHECK(names.size() == 3);
  CHECK(names[0] == "Background");
  CHECK(names[1] == "Middle");
  CHECK(names[2] == "Top");

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 3);
  CHECK(read.layers()[0].name() == "Background");
  CHECK(read.layers()[1].name() == "Middle");
  CHECK(read.layers()[2].name() == "Top");

  patchy::Document no_background(3, 2, patchy::PixelFormat::rgb8());
  no_background.add_pixel_layer("Bottom", solid_rgba(3, 2, 20, 40, 60, 255));
  no_background.add_pixel_layer("Top", solid_rgba(3, 2, 220, 20, 60, 192));

  const auto no_background_bytes = patchy::psd::DocumentIo::write_layered_rgb8(no_background);
  const auto no_background_names = psd_raw_layer_record_names(no_background_bytes);
  CHECK(no_background_names.size() == 2);
  CHECK(no_background_names[0] == "Bottom");
  CHECK(no_background_names[1] == "Top");

  const auto no_background_read = patchy::psd::DocumentIo::read(no_background_bytes);
  CHECK(no_background_read.layers().size() == 2);
  CHECK(no_background_read.layers()[0].name() == "Bottom");
  CHECK(no_background_read.layers()[1].name() == "Top");
}

void psd_reader_tolerates_legacy_patchy_top_to_bottom_background_files() {
  patchy::Document legacy_file_order(3, 2, patchy::PixelFormat::rgb8());
  legacy_file_order.add_pixel_layer("Top", solid_rgba(3, 2, 220, 20, 60, 192));
  legacy_file_order.add_pixel_layer("Background", solid_rgb(3, 2, 255, 255, 255));

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(legacy_file_order);
  const auto names = psd_raw_layer_record_names(bytes);
  CHECK(names.size() == 2);
  CHECK(names[0] == "Top");
  CHECK(names[1] == "Background");

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2);
  CHECK(read.layers()[0].name() == "Background");
  CHECK(read.layers()[1].name() == "Top");
}

void psd_reader_preserves_layer_group_hierarchy() {
  auto write_empty_section_record = [](patchy::psd::BigEndianWriter& layer_info, const std::string& name,
                                       std::uint32_t section_type, const char (&blend_mode)[5]) {
    patchy::psd::BigEndianWriter extra;
    extra.write_u32(0);
    extra.write_u32(0);
    write_pascal_padded(extra, name, 4);
    const auto payload =
        section_type == 3U ? section_divider_payload(section_type) : section_divider_payload(section_type, blend_mode);
    write_test_layer_block(extra, "lsct", payload);

    layer_info.write_u32(0);
    layer_info.write_u32(0);
    layer_info.write_u32(0);
    layer_info.write_u32(0);
    layer_info.write_u16(0);
    write_ascii4(layer_info, "8BIM");
    write_ascii4(layer_info, blend_mode);
    layer_info.write_u8(255);
    layer_info.write_u8(0);
    layer_info.write_u8(0);
    layer_info.write_u8(0);
    layer_info.write_u32(static_cast<std::uint32_t>(extra.bytes().size()));
    layer_info.write_bytes(extra.bytes());
  };

  auto write_pixel_record = [](patchy::psd::BigEndianWriter& layer_info, const std::string& name) {
    patchy::psd::BigEndianWriter extra;
    extra.write_u32(0);
    extra.write_u32(0);
    write_pascal_padded(extra, name, 4);

    layer_info.write_u32(0);
    layer_info.write_u32(0);
    layer_info.write_u32(1);
    layer_info.write_u32(1);
    layer_info.write_u16(3);
    for (std::uint16_t channel = 0; channel < 3; ++channel) {
      layer_info.write_u16(channel);
      layer_info.write_u32(3);
    }
    write_ascii4(layer_info, "8BIM");
    write_ascii4(layer_info, "norm");
    layer_info.write_u8(255);
    layer_info.write_u8(0);
    layer_info.write_u8(0);
    layer_info.write_u8(0);
    layer_info.write_u32(static_cast<std::uint32_t>(extra.bytes().size()));
    layer_info.write_bytes(extra.bytes());
  };

  auto write_pixel_channels = [](patchy::psd::BigEndianWriter& layer_info, std::uint8_t red, std::uint8_t green,
                                 std::uint8_t blue) {
    layer_info.write_u16(0);
    layer_info.write_u8(red);
    layer_info.write_u16(0);
    layer_info.write_u8(green);
    layer_info.write_u16(0);
    layer_info.write_u8(blue);
  };

  patchy::psd::BigEndianWriter layer_info;
  layer_info.write_u16(4);
  write_empty_section_record(layer_info, "</Layer group>", 3, "norm");
  write_pixel_record(layer_info, "Bottom Child");
  write_pixel_record(layer_info, "Top Child");
  write_empty_section_record(layer_info, "Folder", 2, "pass");
  write_pixel_channels(layer_info, 180, 20, 20);
  write_pixel_channels(layer_info, 20, 40, 220);
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
  writer.write_u8(20);
  writer.write_u8(40);
  writer.write_u8(220);

  const auto read = patchy::psd::DocumentIo::read(writer.bytes());
  CHECK(read.layers().size() == 1);
  const auto& folder = read.layers().front();
  CHECK(folder.kind() == patchy::LayerKind::Group);
  CHECK(folder.name() == "Folder");
  CHECK(folder.blend_mode() == patchy::BlendMode::PassThrough);
  CHECK(folder.metadata().at(patchy::kLayerMetadataGroupExpanded) == "false");
  CHECK(folder.children().size() == 2);
  CHECK(folder.children()[0].name() == "Bottom Child");
  CHECK(folder.children()[1].name() == "Top Child");
  CHECK(read.find_layer(folder.children()[0].id()) == &folder.children()[0]);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(read);
  const auto* px = flattened.pixel(0, 0);
  CHECK(px[0] == 20);
  CHECK(px[1] == 40);
  CHECK(px[2] == 220);
}

void psd_writer_round_trips_layer_groups() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(2, 2, 255, 255, 255));

  patchy::Layer group(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  group.set_blend_mode(patchy::BlendMode::PassThrough);
  group.metadata()[patchy::kLayerMetadataGroupExpanded] = "false";
  group.add_child(patchy::Layer(document.allocate_layer_id(), "Bottom Child",
                                   solid_rgba(2, 2, 180, 20, 20, 255)));
  group.add_child(patchy::Layer(document.allocate_layer_id(), "Top Child",
                                   solid_rgba(2, 2, 20, 40, 220, 192)));
  document.add_layer(std::move(group));
  document.add_pixel_layer("Foreground", solid_rgba(2, 2, 10, 200, 40, 128));

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto names = psd_raw_layer_record_names(bytes);
  CHECK(names.size() == 6);
  CHECK(names[0] == "Background");
  CHECK(names[1] == "</Layer group>");
  CHECK(names[2] == "Bottom Child");
  CHECK(names[3] == "Top Child");
  CHECK(names[4] == "Folder");
  CHECK(names[5] == "Foreground");

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 3);
  CHECK(read.layers()[0].name() == "Background");
  CHECK(read.layers()[1].kind() == patchy::LayerKind::Group);
  CHECK(read.layers()[1].name() == "Folder");
  CHECK(read.layers()[1].blend_mode() == patchy::BlendMode::PassThrough);
  CHECK(read.layers()[1].metadata().at(patchy::kLayerMetadataGroupExpanded) == "false");
  CHECK(read.layers()[1].children().size() == 2);
  CHECK(read.layers()[1].children()[0].name() == "Bottom Child");
  CHECK(read.layers()[1].children()[1].name() == "Top Child");
  CHECK(read.layers()[2].name() == "Foreground");

  const auto read_again = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(read));
  CHECK(read_again.layers().size() == 3);
  CHECK(read_again.layers()[1].kind() == patchy::LayerKind::Group);
  CHECK(read_again.layers()[1].children().size() == 2);
  CHECK(read_again.layers()[1].children()[1].name() == "Top Child");
}

void psd_ipad_main_v04_preserves_folders_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("ipad_main_v04.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  const auto document = patchy::psd::DocumentIo::read_file(path);
  CHECK(document.width() == 1024);
  CHECK(document.height() == 768);
  CHECK(document.layers().size() == 5);
  CHECK(document.layers()[0].name() == "BG");
  CHECK(document.layers()[2].name() == "Buttons");
  CHECK(document.layers()[4].name() == "RT Soft small");

  std::function<const patchy::Layer*(const std::vector<patchy::Layer>&, const std::string&)> find_group =
      [&](const std::vector<patchy::Layer>& layers, const std::string& name) -> const patchy::Layer* {
    for (const auto& layer : layers) {
      if (layer.kind() == patchy::LayerKind::Group && layer.name() == name) {
        return &layer;
      }
      if (const auto* found = find_group(layer.children(), name); found != nullptr) {
        return found;
      }
    }
    return nullptr;
  };

  const auto* bg = find_group(document.layers(), "BG");
  const auto* fire = find_group(document.layers(), "Fire");
  const auto* buttons = find_group(document.layers(), "Buttons");
  CHECK(bg != nullptr);
  CHECK(fire != nullptr);
  CHECK(buttons != nullptr);
  CHECK(bg->children().size() == 6);
  CHECK(fire->children().size() == 4);
  CHECK(buttons->children().size() == 14);
  CHECK(buttons->children().front().name() == "Add-on Quests");
  CHECK(buttons->children().back().name() == "Quit copy");
}

void psd_writer_preserves_layer_additional_blocks_and_long_names() {
  const std::string long_name = "Long Photoshop layer name " + std::string(280, 'X');
  const std::string text = "Editable text survives";
  const std::string engine_data =
      "/EngineData << /Editor << /Text (" + text +
      "\\r) >> /StyleRun << /StyleSheetData << /FontSize 42 /FillColor << /Type 1 /Values [ 1.0 1.0 1.0 1.0 ] "
      ">> >> >> >>";
  const auto text_payload =
      std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(engine_data.data()),
                                reinterpret_cast<const std::uint8_t*>(engine_data.data()) + engine_data.size());
  const std::vector<std::uint8_t> custom_payload{9, 8, 7, 6, 5};

  patchy::Document document(3, 2, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer(long_name, solid_rgba(3, 2, 20, 40, 60, 255));
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"zzzz", custom_payload});
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"TySh", text_payload});

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto extra_data = psd_first_layer_extra_data(bytes);
  const std::vector<std::uint8_t> custom_block_header{'8', 'B', 'I', 'M', 'z', 'z', 'z', 'z'};
  const auto custom_block =
      std::search(extra_data.begin(), extra_data.end(), custom_block_header.begin(), custom_block_header.end());
  CHECK(custom_block != extra_data.end());
  const auto custom_block_offset = static_cast<std::size_t>(custom_block - extra_data.begin());
  CHECK(read_u32_be_at(extra_data, custom_block_offset + 8U) == static_cast<std::uint32_t>(custom_payload.size()));
  const auto next_signature_offset = custom_block_offset + 12U + custom_payload.size();
  CHECK(next_signature_offset + 4U <= extra_data.size());
  CHECK(extra_data[next_signature_offset] == static_cast<std::uint8_t>('8'));
  CHECK(extra_data[next_signature_offset + 1U] == static_cast<std::uint8_t>('B'));
  CHECK(extra_data[next_signature_offset + 2U] == static_cast<std::uint8_t>('I'));
  CHECK(extra_data[next_signature_offset + 3U] == static_cast<std::uint8_t>('M'));

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 1);
  CHECK(read.layers().front().name() == long_name);
  CHECK(read.layers().front().metadata().at(patchy::kLayerMetadataText) == text);
  CHECK(read.layers().front().metadata().at(patchy::kLayerMetadataTextSize) == "42");
  CHECK(read.layers().front().metadata().at(patchy::kLayerMetadataTextColor) == "#ffffff");
  CHECK(read.layers().front().metadata().at(patchy::kLayerMetadataTextSourceBlock) == "TySh");
  CHECK(read.layers().front().metadata().at(patchy::kLayerMetadataTextRasterStatus) == "psd_raster_preview");

  bool found_custom = false;
  bool found_text = false;
  bool found_unicode_name = false;
  for (const auto& block : read.layers().front().unknown_psd_blocks()) {
    if (block.key == "zzzz" && block.payload == custom_payload) {
      found_custom = true;
    }
    if (block.key == "TySh" && block.payload == text_payload) {
      found_text = true;
    }
    if (block.key == "luni") {
      found_unicode_name = true;
    }
  }
  CHECK(found_custom);
  CHECK(found_text);
  CHECK(found_unicode_name);

  const auto read_again = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(read));
  CHECK(read_again.layers().size() == 1);
  CHECK(read_again.layers().front().name() == long_name);
  CHECK(read_again.layers().front().metadata().at(patchy::kLayerMetadataText) == text);
}

void psd_import_regenerates_large_styled_text_preview_alpha() {
  constexpr std::int32_t layer_width = 320;
  constexpr std::int32_t layer_height = 150;
  const std::string text = "Clean\nAlpha";

  patchy::Document document(420, 260, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(420, 260, 230, 220, 204));

  auto polluted = solid_rgba(layer_width, layer_height, 243, 237, 230, 0);
  for (std::int32_t y = 10; y < 48; ++y) {
    for (std::int32_t x = 0; x < layer_width; ++x) {
      polluted.pixel(x, y)[3] = 220;
    }
  }
  for (std::int32_t x = 0; x < layer_width; x += 8) {
    for (std::int32_t y = 0; y < layer_height; ++y) {
      polluted.pixel(x, y)[3] = 180;
    }
  }

  patchy::Layer text_layer(document.allocate_layer_id(), "Styled text preview", std::move(polluted));
  text_layer.set_bounds(patchy::Rect{50, 70, layer_width, layer_height});
  text_layer.metadata()[patchy::kLayerMetadataText] = text;
  text_layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t11\t34\t1\t0\t#f3ede6\tArial";
  text_layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] = "v1\n0\t11\tcenter";
  text_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "260";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "90";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "34";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#f3ede6";
  text_layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  text_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Multiply;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 90.0F;
  shadow.distance = 18.0F;
  shadow.spread = 70.0F;
  shadow.size = 80.0F;
  text_layer.layer_style().drop_shadows.push_back(shadow);

  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{184, 81, 74};
  glow.opacity = 1.0F;
  glow.spread = 100.0F;
  glow.size = 72.0F;
  text_layer.layer_style().outer_glows.push_back(glow);

  document.add_layer(std::move(text_layer));
  const auto read = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto* imported = find_layer_named(read.layers(), "Styled text preview");
  CHECK(imported != nullptr);
  CHECK(imported->metadata().at(patchy::kLayerMetadataTextRasterStatus) == "patchy_raster");

  const auto& pixels = imported->pixels();
  std::uint64_t visible_alpha = 0;
  int tall_alpha_columns = 0;
  for (std::int32_t x = 0; x < pixels.width(); ++x) {
    int column_alpha = 0;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      if (pixels.pixel(x, y)[3] > 0U) {
        ++visible_alpha;
        ++column_alpha;
      }
    }
    if (column_alpha * 4 > pixels.height() * 3) {
      ++tall_alpha_columns;
    }
  }

  const auto layer_area = static_cast<std::uint64_t>(pixels.width()) * static_cast<std::uint64_t>(pixels.height());
  CHECK(visible_alpha * 4U < layer_area);
  CHECK(tall_alpha_columns == 0);
}

void psd_writer_exports_patchy_rich_text_as_photoshop_type() {
  patchy::Document document(240, 120, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(240, 120, 255, 255, 255));
  patchy::Layer rich_layer(document.allocate_layer_id(), "Text: Red Blue", solid_rgba(180, 64, 0, 0, 0, 0));
  auto& layer = document.add_layer(std::move(rich_layer));
  layer.set_bounds(patchy::Rect{18, 22, 180, 64});
  layer.metadata()[patchy::kLayerMetadataText] = "Red Blue";
  layer.metadata()[patchy::kLayerMetadataTextHtml] =
      "<html><body><p><span style=\"font-family:'Arial'; font-size:32px; color:#ff2020; font-weight:700;\">Red "
      "</span><span style=\"font-family:'Times New Roman'; font-size:28px; color:#2040ff; font-style:italic;\">Blue"
      "</span></p></body></html>";
  layer.metadata()[patchy::kLayerMetadataTextRuns] =
      "v1\n0\t4\t32\t1\t0\t#ff2020\tArial\n4\t4\t28\t0\t1\t#2040ff\tTimes%20New%20Roman";
  layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] = "v1\n0\t8\tcenter";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "180";
  layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "64";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "32";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#ff2020";
  layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  layer.metadata()[patchy::kLayerMetadataTextItalic] = "false";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"TySh", {9, 9, 9}});

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  const std::vector<std::uint8_t> raw_utf16_marker{'(', 0xFEU, 0xFFU};
  const std::vector<std::uint8_t> octal_utf16_marker{'(', '\\', '3', '7', '6', '\\', '3', '7', '7'};
  CHECK(std::search(text_payload->begin(), text_payload->end(), raw_utf16_marker.begin(), raw_utf16_marker.end()) !=
        text_payload->end());
  CHECK(std::search(text_payload->begin(), text_payload->end(), octal_utf16_marker.begin(),
                    octal_utf16_marker.end()) == text_payload->end());
  const std::string generated_payload_text(text_payload->begin(), text_payload->end());
  CHECK(generated_payload_text.find("/DefaultRunData") != std::string::npos);
  CHECK(generated_payload_text.find("/Rendered") != std::string::npos);
  CHECK(generated_payload_text.find("/DocumentResources") != std::string::npos);
  CHECK(generated_payload_text.find("/ShapeType 1") != std::string::npos);
  CHECK(generated_payload_text.find("/BoxBounds") != std::string::npos);
  CHECK(generated_payload_text.find("/PointBase") == std::string::npos);
  CHECK(generated_payload_text.find("/FontSize 32.000000") != std::string::npos);
  CHECK(generated_payload_text.find("/FontSize 28.000000") != std::string::npos);
  CHECK(text_payload->size() >= 16U);
  const auto text_bounds_offset = text_payload->size() - 16U;
  CHECK(read_u32_be_at(*text_payload, text_bounds_offset) == 18U);
  CHECK(read_u32_be_at(*text_payload, text_bounds_offset + 4U) == 22U);
  CHECK(read_u32_be_at(*text_payload, text_bounds_offset + 8U) == 198U);
  CHECK(read_u32_be_at(*text_payload, text_bounds_offset + 12U) == 86U);

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2);
  const auto& round_tripped_text_layer = read.layers().back();
  CHECK(round_tripped_text_layer.metadata().at(patchy::kLayerMetadataText) == "Red Blue");
  CHECK(round_tripped_text_layer.metadata().at(patchy::kLayerMetadataTextSourceBlock) == "TySh");
  CHECK(round_tripped_text_layer.metadata().at(patchy::kLayerMetadataTextSize) == "32");
  CHECK(round_tripped_text_layer.metadata().at(patchy::kLayerMetadataTextColor) == "#ff2020");
  CHECK(round_tripped_text_layer.metadata().at(patchy::kLayerMetadataTextHtml).find("#ff2020") != std::string::npos);
  CHECK(round_tripped_text_layer.metadata().at(patchy::kLayerMetadataTextHtml).find("#2040ff") != std::string::npos);
  const auto& round_tripped_runs = round_tripped_text_layer.metadata().at(patchy::kLayerMetadataTextRuns);
  CHECK(round_tripped_runs.find("Times%20New%20Roman") != std::string::npos ||
        round_tripped_runs.find("TimesNewRoman") != std::string::npos);
  CHECK(round_tripped_text_layer.metadata().at(patchy::kLayerMetadataTextParagraphRuns).find("center") != std::string::npos);
  CHECK(round_tripped_text_layer.metadata().at(patchy::kLayerMetadataTextFlow) == "box");
  CHECK(round_tripped_text_layer.metadata().at(patchy::kLayerMetadataTextBoxWidth) == "180");
  CHECK(round_tripped_text_layer.metadata().at(patchy::kLayerMetadataTextBoxHeight) == "64");

  bool found_generated_type = false;
  bool found_stale_type = false;
  for (const auto& block : round_tripped_text_layer.unknown_psd_blocks()) {
    if (block.key != "TySh") {
      continue;
    }
    found_stale_type = found_stale_type || block.payload == std::vector<std::uint8_t>{9, 9, 9};
    const std::string payload_text(block.payload.begin(), block.payload.end());
    found_generated_type = payload_text.find("/StyleRun") != std::string::npos &&
                           payload_text.find("/ParagraphRun") != std::string::npos &&
                           payload_text.find("/DocumentResources") != std::string::npos &&
                           payload_text.find("/ShapeType 1") != std::string::npos &&
                           payload_text.find("/BoxBounds") != std::string::npos &&
                           payload_text.find("/Justification 2") != std::string::npos &&
                           payload_text.find("/FauxBold true") != std::string::npos &&
                           payload_text.find("/FauxItalic true") != std::string::npos;
  }
  CHECK(found_generated_type);
  CHECK(!found_stale_type);
}

void psd_writer_preserves_imported_photoshop_text_geometry() {
  patchy::Document document(1280, 720, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(1280, 720, 255, 255, 255));
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: THE INDIE PURSE", solid_rgba(1187, 96, 0, 0, 0, 0));
  auto& layer = document.add_layer(std::move(text_layer));
  layer.set_bounds(patchy::Rect{41, 66, 1187, 96});
  layer.metadata()[patchy::kLayerMetadataText] = "THE INDIE PURSE";
  layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t15\t96\t1\t0\t#ffffff\tAdobeGothicStd-Bold";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "1292";
  layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "541";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  layer.metadata()[patchy::kLayerMetadataPsdTextTransform] =
      "1.0772238306426084 0 0 1.0772238306426084 -61.520306985688736 67.415984778757095";
  layer.metadata()[patchy::kLayerMetadataPsdTextBounds] =
      "0 -16.1993408203125 1291.559814453125 524.6451416015625";
  layer.metadata()[patchy::kLayerMetadataPsdTextBoundingBox] =
      "95.5179443359375 -0.841064453125 1196.88671875 87.596435546875";
  layer.metadata()[patchy::kLayerMetadataPsdTextBoxBounds] =
      "0 0 1291.559814453125 524.6451416015625";
  layer.metadata()[patchy::kLayerMetadataPsdTextTailBounds] = "0 0 0 0";
  layer.metadata()[patchy::kLayerMetadataPsdTextIndex] = "2";

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  CHECK(std::abs(read_f64_be_at(*text_payload, 2U) - 1.0772238306426084) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*text_payload, 34U) - -61.520306985688736) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*text_payload, 42U) - 67.415984778757095) < 0.000001);
  const std::string payload_text(text_payload->begin(), text_payload->end());
  CHECK(payload_text.find("/ShapeType 1") != std::string::npos);
  CHECK(payload_text.find("/BoxBounds [ 0.000000 0.000000 1291.559814 524.645142 ]") != std::string::npos);
  CHECK(payload_text.find("/PointBase") == std::string::npos);
  CHECK(payload_text.find("/FontSize 96.000000") != std::string::npos);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 16U) == 0U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 12U) == 0U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 8U) == 0U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 4U) == 0U);
}

void psd_writer_prefers_patchy_text_transform_over_imported_geometry() {
  patchy::Document document(240, 140, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(240, 140, 255, 255, 255));
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Move Me", solid_rgba(80, 32, 0, 0, 0, 0));
  auto& layer = document.add_layer(std::move(text_layer));
  layer.set_bounds(patchy::Rect{12, 18, 80, 32});
  layer.metadata()[patchy::kLayerMetadataText] = "Move Me";
  layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t7\t32\t0\t0\t#202020\tArial";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "point";
  layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "80";
  layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "32";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  layer.metadata()[patchy::kLayerMetadataPsdTextTransform] = "1 0 0 1 12 18";
  layer.metadata()[patchy::kLayerMetadataPsdTextBounds] = "0 -90 200 40";
  layer.metadata()[patchy::kLayerMetadataPsdTextBoundingBox] = "90 -80 180 -20";
  layer.metadata()[patchy::kLayerMetadataPsdTextBoxBounds] = "0 0 200 130";
  layer.metadata()[patchy::kLayerMetadataPsdTextTailBounds] = "1 2 3 4";
  layer.metadata()[patchy::kLayerMetadataTextTransform] = "1.25 0 0 1.5 42 51";

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  CHECK(std::abs(read_f64_be_at(*text_payload, 2U) - 1.25) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*text_payload, 26U) - 1.5) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*text_payload, 34U) - 42.0) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*text_payload, 42U) - 99.0) < 0.000001);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 16U) == 12U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 12U) == 18U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 8U) == 92U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 4U) == 50U);
}

void psd_writer_exports_point_text_with_photoshop_baseline_origin() {
  patchy::Document document(260, 140, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(260, 140, 255, 255, 255));
  auto pixels = solid_rgba(140, 60, 0, 0, 0, 0);
  for (std::int32_t y = 0; y < 48; ++y) {
    for (std::int32_t x = 0; x < 120; ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = 32;
      pixel[1] = 32;
      pixel[2] = 32;
      pixel[3] = 255;
    }
  }

  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Quick Ass", std::move(pixels));
  auto& layer = document.add_layer(std::move(text_layer));
  layer.set_bounds(patchy::Rect{40, 50, 140, 60});
  layer.metadata()[patchy::kLayerMetadataText] = "Quick Ass";
  layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t9\t72\t0\t0\t#202020\tArial";
  layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] = "v1\n0\t9\tleft";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "point";
  layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "140";
  layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "60";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "72";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  CHECK(std::abs(read_f64_be_at(*text_payload, 34U) - 40.0) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*text_payload, 42U) - 98.0) < 0.000001);
  const std::string payload_text(text_payload->begin(), text_payload->end());
  CHECK(payload_text.find("/PointBase [ 0.0 0.0 ]") != std::string::npos);
  CHECK(payload_text.find("/BoxBounds") == std::string::npos);

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2);
  const auto& imported = read.layers().back();
  const auto visual_bounds =
      parse_bounds_metadata4(imported.metadata().at(patchy::kLayerMetadataPsdTextBoundingBox));
  CHECK(std::abs(visual_bounds[0]) < 0.001);
  CHECK(std::abs(visual_bounds[1] + 48.0) < 0.001);
  CHECK(std::abs(visual_bounds[2] - 120.0) < 0.001);
  CHECK(std::abs(visual_bounds[3] - 0.0) < 0.001);

  const auto second_payload =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(read), 1), "TySh");
  CHECK(second_payload.has_value());
  CHECK(std::abs(read_f64_be_at(*second_payload, 34U) - 40.0) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*second_payload, 42U) - 98.0) < 0.000001);
}

void psd_writer_maps_text_raster_bounds_into_transform_local_space() {
  patchy::Document document(360, 220, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(360, 220, 255, 255, 255));
  auto pixels = solid_rgba(220, 115, 0, 0, 0, 0);
  for (std::int32_t y = 0; y < 95; ++y) {
    for (std::int32_t x = 0; x < 150; ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = 32;
      pixel[1] = 32;
      pixel[2] = 32;
      pixel[3] = 255;
    }
  }

  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Offset Preview", std::move(pixels));
  auto& layer = document.add_layer(std::move(text_layer));
  layer.set_bounds(patchy::Rect{50, 50, 220, 115});
  layer.metadata()[patchy::kLayerMetadataText] = "Offset Preview";
  layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t14\t28\t1\t0\t#202020\tArial";
  layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] = "v1\n0\t14\tleft";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "220";
  layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "80";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "28";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  layer.metadata()[patchy::kLayerMetadataTextTransform] = "1 0 0 1 50 60";

  const auto read = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  CHECK(read.layers().size() == 2);
  const auto& imported = read.layers().back();
  const auto box_bounds = parse_bounds_metadata4(imported.metadata().at(patchy::kLayerMetadataPsdTextBoxBounds));
  CHECK(std::abs(box_bounds[0] - 0.0) < 0.001);
  CHECK(std::abs(box_bounds[1] - 0.0) < 0.001);
  CHECK(std::abs(box_bounds[2] - 220.0) < 0.001);
  CHECK(std::abs(box_bounds[3] - 80.0) < 0.001);
  const auto visual_bounds =
      parse_bounds_metadata4(imported.metadata().at(patchy::kLayerMetadataPsdTextBoundingBox));
  CHECK(std::abs(visual_bounds[0] - 0.0) < 0.001);
  CHECK(std::abs(visual_bounds[1] + 10.0) < 0.001);
  CHECK(std::abs(visual_bounds[2] - 150.0) < 0.001);
  CHECK(std::abs(visual_bounds[3] - 85.0) < 0.001);
}

void psd_writer_ignores_stale_imported_geometry_for_patchy_owned_text_frame() {
  const std::string text = "Expanded imported text frame";

  patchy::Document document(360, 220, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(360, 220, 255, 255, 255));
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Expanded imported",
                           solid_rgba(260, 96, 0, 0, 0, 0));
  auto& layer = document.add_layer(std::move(text_layer));
  layer.set_bounds(patchy::Rect{24, 30, 260, 96});
  layer.metadata()[patchy::kLayerMetadataText] = text;
  layer.metadata()[patchy::kLayerMetadataTextRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\t28\t1\t0\t#202020\tArial";
  layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\tleft";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "260";
  layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "96";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "28";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  layer.metadata()[patchy::kLayerMetadataTextItalic] = "false";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  layer.metadata()[patchy::kLayerMetadataTextTransform] = "1 0 0 1 24 30";
  layer.metadata()[patchy::kLayerMetadataPsdTextTransform] = "1 0 0 1 24 30";
  layer.metadata()[patchy::kLayerMetadataPsdTextBounds] = "0 0 128 40";
  layer.metadata()[patchy::kLayerMetadataPsdTextBoundingBox] = "0 0 128 40";
  layer.metadata()[patchy::kLayerMetadataPsdTextBoxBounds] = "0 0 128 40";
  layer.metadata()[patchy::kLayerMetadataPsdTextTailBounds] = "24 30 152 70";

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  CHECK(std::abs(read_f64_be_at(*text_payload, 34U) - 24.0) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*text_payload, 42U) - 30.0) < 0.000001);
  const std::string payload_text(text_payload->begin(), text_payload->end());
  CHECK(payload_text.find("/BoxBounds [ 0.000000 0.000000 260.000000 96.000000 ]") != std::string::npos);
  CHECK(payload_text.find("/BoxBounds [ 0.000000 0.000000 128.000000 40.000000 ]") == std::string::npos);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 16U) == 24U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 12U) == 30U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 8U) == 284U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 4U) == 126U);

  const auto read = patchy::psd::DocumentIo::read(bytes);
  const auto* round_tripped = find_layer_named(read.layers(), "Text: Expanded imported");
  CHECK(round_tripped != nullptr);
  CHECK(round_tripped->metadata().at(patchy::kLayerMetadataText) == text);
  CHECK(round_tripped->metadata().at(patchy::kLayerMetadataTextFlow) == "box");
  CHECK(round_tripped->metadata().at(patchy::kLayerMetadataTextBoxWidth) == "260");
  CHECK(round_tripped->metadata().at(patchy::kLayerMetadataTextBoxHeight) == "96");
  CHECK(round_tripped->metadata().at(patchy::kLayerMetadataTextTransform) == "1 0 0 1 24 30");

  const auto second_payload =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(read), 1), "TySh");
  CHECK(second_payload.has_value());
  const std::string second_payload_text(second_payload->begin(), second_payload->end());
  CHECK(second_payload_text.find("/BoxBounds [ 0.000000 0.000000 260.000000 96.000000 ]") !=
        std::string::npos);
  CHECK(std::abs(read_f64_be_at(*second_payload, 34U) - 24.0) < 0.000001);
  CHECK(std::abs(read_f64_be_at(*second_payload, 42U) - 30.0) < 0.000001);
}

void psd_writer_regenerates_same_length_patchy_text_without_stale_template() {
  patchy::Document source_document(240, 120, patchy::PixelFormat::rgb8());
  source_document.add_pixel_layer("Background", solid_rgb(240, 120, 255, 255, 255));
  patchy::Layer source_text(source_document.allocate_layer_id(), "Text: THE INDIE CURSE",
                           solid_rgba(180, 64, 0, 0, 0, 0));
  auto& source_layer = source_document.add_layer(std::move(source_text));
  source_layer.set_bounds(patchy::Rect{18, 22, 180, 64});
  source_layer.metadata()[patchy::kLayerMetadataText] = "THE INDIE CURSE";
  source_layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t15\t32\t1\t0\t#ff2020\tArial";
  source_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  source_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "180";
  source_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "64";
  source_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";

  auto source_payload =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(source_document), 1),
                              "TySh")
          .value();
  std::fill(source_payload.end() - 16, source_payload.end(), std::uint8_t{0});

  patchy::Document edited_document(240, 120, patchy::PixelFormat::rgb8());
  edited_document.add_pixel_layer("Background", solid_rgb(240, 120, 255, 255, 255));
  patchy::Layer edited_text(edited_document.allocate_layer_id(), "Text: THE INDIE PURSE",
                            solid_rgba(180, 64, 0, 0, 0, 0));
  auto& edited_layer = edited_document.add_layer(std::move(edited_text));
  edited_layer.set_bounds(patchy::Rect{18, 22, 180, 64});
  edited_layer.metadata()[patchy::kLayerMetadataText] = "THE INDIE PURSE";
  edited_layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t15\t32\t1\t0\t#ff2020\tArial";
  edited_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  edited_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "180";
  edited_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "64";
  edited_layer.metadata()[patchy::kLayerMetadataTextSourceBlock] = "TySh";
  edited_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  edited_layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"TySh", source_payload});

  const auto edited_payload =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(edited_document), 1),
                              "TySh")
          .value();
  const auto old_text = utf16be_test_bytes("THE INDIE CURSE");
  const auto new_text = utf16be_test_bytes("THE INDIE PURSE");
  CHECK(std::search(edited_payload.begin(), edited_payload.end(), old_text.begin(), old_text.end()) ==
        edited_payload.end());
  CHECK(std::search(edited_payload.begin(), edited_payload.end(), new_text.begin(), new_text.end()) !=
        edited_payload.end());
  const std::string payload_text(edited_payload.begin(), edited_payload.end());
  CHECK(payload_text.find("/FontSize 32.000000") != std::string::npos);
  CHECK(payload_text.find("/AutoLeading true /Leading 38.400000") != std::string::npos);
}

void psd_writer_ignores_stale_type_template_after_patchy_text_edit() {
  patchy::Document source_document(240, 120, patchy::PixelFormat::rgb8());
  source_document.add_pixel_layer("Background", solid_rgb(240, 120, 255, 255, 255));
  patchy::Layer source_text(source_document.allocate_layer_id(), "Text: THE INDIE CURSE",
                           solid_rgba(180, 64, 0, 0, 0, 0));
  auto& source_layer = source_document.add_layer(std::move(source_text));
  source_layer.set_bounds(patchy::Rect{18, 22, 180, 64});
  source_layer.metadata()[patchy::kLayerMetadataText] = "THE INDIE CURSE";
  source_layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t15\t32\t0\t0\t#ff2020\tCourier%20New";
  source_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  source_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "180";
  source_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "64";
  source_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  const auto source_payload =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(source_document), 1),
                              "TySh")
          .value();

  patchy::Document edited_document(240, 120, patchy::PixelFormat::rgb8());
  edited_document.add_pixel_layer("Background", solid_rgb(240, 120, 255, 255, 255));
  patchy::Layer edited_text(edited_document.allocate_layer_id(), "Text: THE INDIE PURSE",
                            solid_rgba(180, 64, 0, 0, 0, 0));
  auto& edited_layer = edited_document.add_layer(std::move(edited_text));
  edited_layer.set_bounds(patchy::Rect{18, 22, 180, 64});
  edited_layer.metadata()[patchy::kLayerMetadataText] = "THE INDIE PURSE";
  edited_layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t15\t32\t0\t0\t#ff2020\tArial";
  edited_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  edited_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "180";
  edited_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "64";
  edited_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  edited_layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"TySh", source_payload});

  const auto edited_payload =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(edited_document), 1),
                              "TySh")
          .value();
  const auto old_text = utf16be_test_bytes("THE INDIE CURSE");
  const auto new_text = utf16be_test_bytes("THE INDIE PURSE");
  CHECK(std::search(edited_payload.begin(), edited_payload.end(), old_text.begin(), old_text.end()) ==
        edited_payload.end());
  CHECK(std::search(edited_payload.begin(), edited_payload.end(), new_text.begin(), new_text.end()) !=
        edited_payload.end());

  const std::string payload_text(edited_payload.begin(), edited_payload.end());
  CHECK(payload_text.find("/FontSize 32.000000") != std::string::npos);
#ifdef _WIN32
  const auto arial_postscript = utf16be_test_bytes("ArialMT");
  const auto courier_family = utf16be_test_bytes("Courier");
  CHECK(std::search(edited_payload.begin(), edited_payload.end(), arial_postscript.begin(), arial_postscript.end()) !=
        edited_payload.end());
  CHECK(std::search(edited_payload.begin(), edited_payload.end(), courier_family.begin(), courier_family.end()) ==
        edited_payload.end());
#endif
}

void psd_extended_blend_modes_round_trip() {
  const std::vector<patchy::BlendMode> modes = {
      patchy::BlendMode::Darken,     patchy::BlendMode::Lighten,
      patchy::BlendMode::ColorDodge, patchy::BlendMode::ColorBurn,
      patchy::BlendMode::HardLight,  patchy::BlendMode::SoftLight,
      patchy::BlendMode::Difference, patchy::BlendMode::LinearBurn,
      patchy::BlendMode::PinLight,   patchy::BlendMode::Saturation,
      patchy::BlendMode::Luminosity,
  };

  for (const auto mode : modes) {
    patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(2, 2, 120, 120, 120));
    auto& top = document.add_pixel_layer("Top", solid_rgba(2, 2, 200, 60, 100, 255));
    top.set_blend_mode(mode);

    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    const auto read = patchy::psd::DocumentIo::read(bytes);
    CHECK(read.layers().size() == 2);
    CHECK(read.layers()[1].blend_mode() == mode);
  }
}

void psd_text_layer_engine_data_renders_placeholder_text() {
  const std::string text = "Patchy Text";
  const std::string engine_data =
      "/EngineData << /Editor << /Text (" + text +
      "\\r) >> /StyleRun << /StyleSheetData << /FontSize 36 /FillColor << /Type 1 /Values [ 1.0 .87059 .87059 .87059 ] "
      ">> >> >> >>";
  const auto payload =
      std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(engine_data.data()),
                                reinterpret_cast<const std::uint8_t*>(engine_data.data()) + engine_data.size());

  patchy::psd::BigEndianWriter layer_extra;
  layer_extra.write_u32(0);
  layer_extra.write_u32(0);
  write_pascal_padded(layer_extra, "Text Layer", 4);
  write_ascii4(layer_extra, "8BIM");
  write_ascii4(layer_extra, "TySh");
  layer_extra.write_u32(static_cast<std::uint32_t>(payload.size()));
  layer_extra.write_bytes(payload);
  if ((payload.size() % 2U) != 0) {
    layer_extra.write_u8(0);
  }

  patchy::psd::BigEndianWriter layer_info;
  layer_info.write_u16(1);
  layer_info.write_u32(12);
  layer_info.write_u32(10);
  layer_info.write_u32(42);
  layer_info.write_u32(190);
  layer_info.write_u16(0);
  write_ascii4(layer_info, "8BIM");
  write_ascii4(layer_info, "norm");
  layer_info.write_u8(255);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u8(0);
  layer_info.write_u32(static_cast<std::uint32_t>(layer_extra.bytes().size()));
  layer_info.write_bytes(layer_extra.bytes());
  if ((layer_info.bytes().size() % 2U) != 0) {
    layer_info.write_u8(0);
  }

  patchy::psd::BigEndianWriter layer_mask;
  layer_mask.write_u32(static_cast<std::uint32_t>(layer_info.bytes().size()));
  layer_mask.write_bytes(layer_info.bytes());
  layer_mask.write_u32(0);

  constexpr std::uint32_t width = 220;
  constexpr std::uint32_t height = 80;
  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 3, height, width, 8, 3});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(layer_mask.bytes().size()));
  writer.write_bytes(layer_mask.bytes());
  writer.write_u16(0);
  for (std::size_t i = 0; i < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U; ++i) {
    writer.write_u8(255);
  }

  const auto read = patchy::psd::DocumentIo::read(writer.bytes());
  CHECK(read.layers().size() == 1);
  const auto& layer = read.layers().front();
  CHECK(layer.name() == "Text Layer");
  CHECK(layer.metadata().at(patchy::kLayerMetadataText) == text);
  CHECK(layer.metadata().at(patchy::kLayerMetadataTextSize) == "36");
  CHECK(layer.metadata().at(patchy::kLayerMetadataTextColor) == "#dedede");
  CHECK(layer.metadata().at(patchy::kLayerMetadataTextSourceBlock) == "TySh");
  CHECK(layer.metadata().at(patchy::kLayerMetadataTextRasterStatus) == "placeholder");
  CHECK(layer.pixels().format() == patchy::PixelFormat::rgba8());
  CHECK(layer.bounds().x == 10);
  CHECK(layer.bounds().y == 12);

  bool has_text_pixels = false;
  for (std::size_t offset = 3; offset < layer.pixels().data().size(); offset += 4) {
    if (layer.pixels().data()[offset] != 0U) {
      has_text_pixels = true;
      break;
    }
  }
  CHECK(has_text_pixels);
}

void psd_text_engine_data_normalizes_photoshop_line_breaks_and_font_style() {
  std::string raw_text = "One\rTwo";
  raw_text.push_back('\x03');
  raw_text += "Three\r";
  const auto text_literal = engine_utf16be_literal(raw_text);
  const auto font_literal = engine_utf16be_literal("Verdana-Bold");
  const std::string engine_data =
      "<< /EngineDict << /Editor << /Text " + text_literal +
      " >> /StyleRun << /RunArray [ << /StyleSheet << /StyleSheetData << /Font 0 /FontSize 20 "
      "/FauxBold false /FauxItalic false /FillColor << /Type 1 /Values [ 1.0 0.0 0.0 0.0 ] >> "
      ">> >> >> ] /RunLengthArray [ 14 ] >> /ParagraphRun << /RunArray [ << /ParagraphSheet << "
      "/Properties << /Justification 0 >> >> >> ] /RunLengthArray [ 14 ] >> /AntiAlias 3 "
      "/UseFractionalGlyphWidths true /FontSet [ << /Name " +
      font_literal + " /Script 0 /FontType 1 /Synthetic 0 >> ] >>";
  const auto payload =
      std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(engine_data.data()),
                                reinterpret_cast<const std::uint8_t*>(engine_data.data()) + engine_data.size());

  const auto read = patchy::psd::DocumentIo::read(single_text_layer_psd(payload));
  CHECK(read.layers().size() == 1);
  const auto& layer = read.layers().front();
  const auto& metadata = layer.metadata();
  CHECK(metadata.at(patchy::kLayerMetadataText) == "One\nTwo\nThree");
  CHECK(metadata.at(patchy::kLayerMetadataText).find('\r') == std::string::npos);
  CHECK(metadata.at(patchy::kLayerMetadataText).find('\x03') == std::string::npos);
  CHECK(metadata.at(patchy::kLayerMetadataTextFont) == "Verdana");
  CHECK(metadata.at(patchy::kLayerMetadataTextSize) == "20");
  CHECK(metadata.at(patchy::kLayerMetadataTextColor) == "#000000");
  CHECK(metadata.at(patchy::kLayerMetadataTextBold) == "true");
  CHECK(metadata.at(patchy::kLayerMetadataTextItalic) == "false");
  CHECK(metadata.at(patchy::kLayerMetadataTextAntiAlias) == "3");
  CHECK(metadata.at(patchy::kLayerMetadataTextHtml).find("<br") != std::string::npos);
  CHECK(metadata.at(patchy::kLayerMetadataTextRuns).find("0\t13\t20\t1\t0\t#000000\tVerdana") !=
        std::string::npos);
}

void psd_text_engine_data_preserves_paragraph_layout_runs() {
  const std::string first = "Speed Mode - Hold down TAB and the entire game will run faster.";
  const std::string second = "Saving your game - Find a Save Machine and use it.";
  const std::string third = "Quick state saves - F5 to save and F9 to load";
  std::string raw_text = first + "\r" + second + "\r" + third;
  raw_text.push_back('\x03');
  raw_text.push_back('\x03');
  raw_text.push_back('\r');

  const auto text_literal = engine_utf16be_literal(raw_text);
  const auto font_literal = engine_utf16be_literal("Arial-BoldMT");
  const auto paragraph_properties = [](int space_after) {
    return std::string("<< /ParagraphSheet << /DefaultStyleSheet 0 /Properties << /Justification 0 "
                       "/FirstLineIndent -24.0 /StartIndent 24.0 /EndIndent 0.0 /SpaceBefore 0.0 /SpaceAfter ") +
           std::to_string(space_after) +
           ".0 /AutoHyphenate false /Hanging true >> >> /Adjustments << /Axis [ 1.0 0.0 1.0 ] "
           "/XY [ 0.0 0.0 ] >> >>";
  };
  const auto first_length = static_cast<int>(first.size()) + 1;
  const auto second_length = static_cast<int>(second.size()) + 1;
  const auto third_engine_length = static_cast<int>(third.size()) + 1;
  const auto normalized_text = first + "\n" + second + "\n" + third;
  const std::string engine_data =
      "<< /EngineDict << /Editor << /Text " + text_literal +
      " >> /ParagraphRun << /RunArray [ " + paragraph_properties(24) + " " + paragraph_properties(24) +
      " " + paragraph_properties(0) + " " + paragraph_properties(0) + " " + paragraph_properties(0) +
      " ] /RunLengthArray [ " + std::to_string(first_length) + ' ' + std::to_string(second_length) + ' ' +
      std::to_string(third_engine_length) + " 1 1 ] >> /StyleRun << /RunArray [ << /StyleSheet << "
      "/StyleSheetData << /Font 0 /FontSize 28 /FauxBold false /FauxItalic false "
      "/AutoLeading true /Leading 86.4 "
      "/FillColor << /Type 1 /Values [ 1.0 0.0 0.0 0.0 ] >> >> >> >> ] /RunLengthArray [ " +
      std::to_string(raw_text.size()) + " ] >> /AntiAlias 3 /FontSet [ << /Name " + font_literal +
      " /Script 0 /FontType 1 /Synthetic 0 >> ] >>";
  const auto payload =
      std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(engine_data.data()),
                                reinterpret_cast<const std::uint8_t*>(engine_data.data()) + engine_data.size());

  const auto read = patchy::psd::DocumentIo::read(single_text_layer_psd(payload));
  CHECK(read.layers().size() == 1);
  const auto& metadata = read.layers().front().metadata();
  CHECK(metadata.at(patchy::kLayerMetadataText) == normalized_text);

  const auto& paragraph_runs = metadata.at(patchy::kLayerMetadataTextParagraphRuns);
  const auto third_start = first_length + second_length;
  const std::string expected_runs =
      "v2\n0\t" + std::to_string(first_length) + "\tleft\t-24\t24\t0\t0\t24\n" +
      std::to_string(first_length) + '\t' + std::to_string(second_length) + "\tleft\t-24\t24\t0\t0\t24\n" +
      std::to_string(third_start) + '\t' + std::to_string(third.size()) + "\tleft\t-24\t24\t0\t0\t0";
  CHECK(paragraph_runs == expected_runs);
  const auto& text_runs = metadata.at(patchy::kLayerMetadataTextRuns);
  CHECK(text_runs.find("v2\n") == 0);
  CHECK(text_runs.find("\t86.400000000000006") != std::string::npos ||
        text_runs.find("\t86.4") != std::string::npos);

  auto regenerated = read;
  regenerated.layers().front().metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  const auto regenerated_payload = psd_layer_block_payload(
      psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(regenerated), 0), "TySh");
  CHECK(regenerated_payload.has_value());
  const std::string regenerated_payload_text(regenerated_payload->begin(), regenerated_payload->end());
  CHECK(regenerated_payload_text.find("/Leading 86.400000") != std::string::npos);
}

void psd_text_engine_data_humanizes_postscript_font_family_names() {
  const std::string text = "Metal Slug 3\r";
  const auto text_literal = engine_utf16be_literal(text);
  const auto font_literal = engine_utf16be_literal("NotoNaskhArabic-Bold");
  const std::string engine_data =
      "<< /EngineDict << /Editor << /Text " + text_literal +
      " >> /StyleRun << /RunArray [ << /StyleSheet << /StyleSheetData << /Font 0 /FontSize 113 "
      "/FauxBold false /FauxItalic false /FillColor << /Type 1 /Values [ 1.0 0.0 0.0 0.0 ] >> "
      ">> >> >> ] /RunLengthArray [ 13 ] >> /FontSet [ << /Name " +
      font_literal + " /Script 0 /FontType 1 /Synthetic 0 >> ] >>";
  const auto payload =
      std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(engine_data.data()),
                                reinterpret_cast<const std::uint8_t*>(engine_data.data()) + engine_data.size());

  const auto read = patchy::psd::DocumentIo::read(single_text_layer_psd(payload));
  CHECK(read.layers().size() == 1);
  const auto& metadata = read.layers().front().metadata();
  CHECK(metadata.at(patchy::kLayerMetadataTextFont) == "Noto Naskh Arabic");
  CHECK(metadata.at(patchy::kLayerMetadataTextBold) == "true");
  CHECK(metadata.at(patchy::kLayerMetadataTextRuns).find("0\t12\t113\t1\t0\t#000000\tNoto%20Naskh%20Arabic") !=
        std::string::npos);
}

void psd_text_engine_data_resolves_hyphenated_font_family_names() {
  const std::string text = "Continue\r";
  const auto text_literal = engine_utf16be_literal(text);
  const auto font_literal = engine_utf16be_literal("FZ-SCRIPT25");
  const std::string engine_data =
      "<< /EngineDict << /Editor << /Text " + text_literal +
      " >> /StyleRun << /RunArray [ << /StyleSheet << /StyleSheetData << /Font 0 /FontSize 72 "
      "/FauxBold false /FauxItalic false /FillColor << /Type 1 /Values [ 1.0 0.0 0.0 0.0 ] >> "
      ">> >> >> ] /RunLengthArray [ 9 ] >> /FontSet [ << /Name " +
      font_literal + " /Script 0 /FontType 1 /Synthetic 0 >> ] >>";
  const auto payload =
      std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(engine_data.data()),
                                reinterpret_cast<const std::uint8_t*>(engine_data.data()) + engine_data.size());

  const auto read = patchy::psd::DocumentIo::read(single_text_layer_psd(payload));
  CHECK(read.layers().size() == 1);
  const auto& metadata = read.layers().front().metadata();
  CHECK(metadata.at(patchy::kLayerMetadataTextFont) == "FZ SCRIPT 25");
  CHECK(metadata.at(patchy::kLayerMetadataTextRuns).find("0\t8\t72\t0\t0\t#000000\tFZ%20SCRIPT%2025") !=
        std::string::npos);
}

void psd_writer_emits_photoshop_text_line_breaks() {
  patchy::Document document(240, 120, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(240, 120, 255, 255, 255));
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Lines", solid_rgba(180, 64, 0, 0, 0, 0));
  auto& layer = document.add_layer(std::move(text_layer));
  layer.set_bounds(patchy::Rect{12, 18, 180, 64});
  layer.metadata()[patchy::kLayerMetadataText] = "Line One\nLine Two";
  layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t17\t20\t1\t0\t#000000\tVerdana";
  layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] = "v1\n0\t17\tleft";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "180";
  layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "64";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Verdana";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "20";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#000000";
  layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  layer.metadata()[patchy::kLayerMetadataTextItalic] = "false";
  layer.metadata()[patchy::kLayerMetadataTextAntiAlias] = "3";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  const auto photoshop_text = utf16be_test_bytes("Line One\rLine Two\r");
  const auto patchy_text = utf16be_test_bytes("Line One\nLine Two");
  CHECK(std::search(text_payload->begin(), text_payload->end(), photoshop_text.begin(), photoshop_text.end()) !=
        text_payload->end());
  CHECK(std::search(text_payload->begin(), text_payload->end(), patchy_text.begin(), patchy_text.end()) ==
        text_payload->end());
  const std::string payload_text(text_payload->begin(), text_payload->end());
  CHECK(payload_text.find("/StyleRun") != std::string::npos);
  CHECK(payload_text.find("/ParagraphRun") != std::string::npos);
  CHECK(payload_text.find("/FontSet") != std::string::npos);
  CHECK(payload_text.find("/AntiAlias 3") != std::string::npos);
  CHECK(payload_text.find("/DocumentResources") != std::string::npos);
  const auto split_run_lengths = payload_text.find("/RunLengthArray [ 9 9 ]");
  CHECK(split_run_lengths != std::string::npos);
  CHECK(payload_text.find("/RunLengthArray [ 9 9 ]", split_run_lengths + 1U) != std::string::npos);

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2);
  CHECK(read.layers().back().metadata().at(patchy::kLayerMetadataText) == "Line One\nLine Two");
}

void psd_writer_emits_v2_paragraph_layout() {
  const std::string first = "Speed Mode - Hold down TAB";
  const std::string second = "faster. Good for skipping boring stuff.";
  const std::string text = first + "\n" + second;
  const auto first_length = static_cast<int>(first.size()) + 1;
  const auto second_length = static_cast<int>(second.size());

  patchy::Document document(360, 180, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(360, 180, 255, 255, 255));
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Layout", solid_rgba(260, 120, 0, 0, 0, 0));
  auto& layer = document.add_layer(std::move(text_layer));
  layer.set_bounds(patchy::Rect{40, 50, 260, 120});
  layer.metadata()[patchy::kLayerMetadataText] = text;
  layer.metadata()[patchy::kLayerMetadataTextRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\t28\t1\t0\t#202020\tArial";
  layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] =
      "v2\n0\t" + std::to_string(first_length) + "\tleft\t-24\t24\t0\t0\t24\n" +
      std::to_string(first_length) + '\t' + std::to_string(second_length) + "\tleft\t-24\t24\t0\t0\t0";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "260";
  layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "120";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "28";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  layer.metadata()[patchy::kLayerMetadataTextItalic] = "false";
  layer.metadata()[patchy::kLayerMetadataTextAntiAlias] = "3";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto text_payload = psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh");
  CHECK(text_payload.has_value());
  const std::string payload_text(text_payload->begin(), text_payload->end());
  CHECK(payload_text.find("/FirstLineIndent -24") != std::string::npos);
  CHECK(payload_text.find("/StartIndent 24") != std::string::npos);
  CHECK(payload_text.find("/SpaceAfter 24") != std::string::npos);
  CHECK(payload_text.find("/Hanging true") != std::string::npos);
  CHECK(payload_text.find("/AutoLeading true /Leading 33.600000") != std::string::npos);
  CHECK(payload_text.find("/RunLengthArray [ " + std::to_string(first_length) + ' ' +
                          std::to_string(second_length + 1) + " ]") != std::string::npos);

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2);
  const auto& paragraph_runs = read.layers().back().metadata().at(patchy::kLayerMetadataTextParagraphRuns);
  CHECK(paragraph_runs.find("v2\n0\t" + std::to_string(first_length) + "\tleft\t-24\t24\t0\t0\t24") == 0);
}

void psd_reader_regenerates_patchy_generated_type_blocks_after_reopen() {
  const std::string first = "Speed Mode - Hold down TAB";
  const std::string second = "faster. Good for skipping boring stuff.";
  const std::string text = first + "\n" + second;
  const auto first_length = static_cast<int>(first.size()) + 1;
  const auto second_length = static_cast<int>(second.size());

  patchy::Document document(360, 180, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(360, 180, 255, 255, 255));
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Layout", solid_rgba(260, 120, 32, 32, 32, 255));
  auto& layer = document.add_layer(std::move(text_layer));
  layer.set_bounds(patchy::Rect{40, 50, 260, 120});
  layer.metadata()[patchy::kLayerMetadataText] = text;
  layer.metadata()[patchy::kLayerMetadataTextRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\t28\t1\t0\t#202020\tArial";
  layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] =
      "v2\n0\t" + std::to_string(first_length) + "\tleft\t-24\t24\t0\t0\t24\n" +
      std::to_string(first_length) + '\t' + std::to_string(second_length) + "\tleft\t-24\t24\t0\t0\t0";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "260";
  layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "120";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "28";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  layer.metadata()[patchy::kLayerMetadataTextItalic] = "false";
  layer.metadata()[patchy::kLayerMetadataTextAntiAlias] = "3";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";

  auto stale_bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const std::string leading_marker = "/AutoLeading true /Leading 33.600000";
  const std::string blank_leading(leading_marker.size(), ' ');
  CHECK(replace_all_ascii_same_length(stale_bytes, leading_marker, blank_leading) > 0);

  const auto stale_payload = psd_layer_block_payload(psd_layer_extra_data(stale_bytes, 1), "TySh");
  CHECK(stale_payload.has_value());
  const std::string stale_payload_text(stale_payload->begin(), stale_payload->end());
  CHECK(stale_payload_text.find(leading_marker) == std::string::npos);
  CHECK(stale_payload_text.find("/KinsokuSet [ ] /MojiKumiSet [ ] /TheNormalStyleSheet 0") != std::string::npos);

  const auto read = patchy::psd::DocumentIo::read(stale_bytes);
  CHECK(read.layers().size() == 2);
  const auto& read_layer = read.layers().back();
  CHECK(read_layer.metadata().at(patchy::kLayerMetadataText) == text);
  CHECK(read_layer.metadata().at(patchy::kLayerMetadataTextSourceBlock) == "TySh");
  CHECK(read_layer.metadata().at(patchy::kLayerMetadataTextRasterStatus) == "patchy_raster");

  const auto regenerated_bytes = patchy::psd::DocumentIo::write_layered_rgb8(read);
  const auto regenerated_payload = psd_layer_block_payload(psd_layer_extra_data(regenerated_bytes, 1), "TySh");
  CHECK(regenerated_payload.has_value());
  const std::string regenerated_payload_text(regenerated_payload->begin(), regenerated_payload->end());
  CHECK(regenerated_payload_text.find(leading_marker) != std::string::npos);
  CHECK(regenerated_payload_text.find("/SpaceAfter 24") != std::string::npos);

  const auto read_again = patchy::psd::DocumentIo::read(regenerated_bytes);
  CHECK(read_again.layers().size() == 2);
  const auto& read_again_layer = read_again.layers().back();
  CHECK(read_again_layer.metadata().at(patchy::kLayerMetadataText) == text);
  CHECK(read_again_layer.metadata().at(patchy::kLayerMetadataTextRasterStatus) == "patchy_raster");
  const auto regenerated_again_payload =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(read_again), 1), "TySh");
  CHECK(regenerated_again_payload.has_value());
  const std::string regenerated_again_payload_text(regenerated_again_payload->begin(), regenerated_again_payload->end());
  CHECK(regenerated_again_payload_text.find(leading_marker) != std::string::npos);
}

void psd_horror_virtualboy_imports_multiline_bold_text_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("Horror VirtualBoy.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  const auto document = patchy::psd::DocumentIo::read_file(path);
  bool found_body_text = false;
  std::function<void(const std::vector<patchy::Layer>&)> visit = [&](const std::vector<patchy::Layer>& layers) {
    for (const auto& layer : layers) {
      if (const auto text = layer.metadata().find(patchy::kLayerMetadataText);
          text != layer.metadata().end() && text->second.find("Did you know") != std::string::npos) {
        found_body_text = true;
        CHECK(text->second.find('\n') != std::string::npos);
        CHECK(text->second.find('\r') == std::string::npos);
        CHECK(text->second.find('\x03') == std::string::npos);
        CHECK(layer.metadata().at(patchy::kLayerMetadataTextFont) == "Verdana");
        CHECK(layer.metadata().at(patchy::kLayerMetadataTextBold) == "true");
        CHECK(layer.metadata().at(patchy::kLayerMetadataTextAntiAlias) == "3");
      }
      visit(layer.children());
    }
  };
  visit(document.layers());
  CHECK(found_body_text);
}

void psd_arduboy_real_file_renders_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("Arduboy.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  const auto document = patchy::psd::DocumentIo::read_file(path);
  CHECK(document.width() == 2550);
  CHECK(document.height() == 3300);
  CHECK(document.layers().size() == 4);

  int text_layers = 0;
  for (const auto& layer : document.layers()) {
    if (layer.metadata().contains(patchy::kLayerMetadataText)) {
      ++text_layers;
    }
  }
  CHECK(text_layers >= 2);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  std::size_t non_white_pixels = 0;
  for (std::int32_t y = 0; y < flattened.height(); y += 12) {
    for (std::int32_t x = 0; x < flattened.width(); x += 12) {
      const auto* px = flattened.pixel(x, y);
      if (px[0] < 245 || px[1] < 245 || px[2] < 245) {
        ++non_white_pixels;
      }
    }
  }
  CHECK(non_white_pixels > 1000);
}

void psd_title_screen_demo_layer_styles_render_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("Title Screen_demo.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  const auto document = patchy::psd::DocumentIo::read_file(path);
  CHECK(document.width() == 640);
  CHECK(document.height() == 480);

  int styled_layers = 0;
  int gradient_layers = 0;
  int shadow_layers = 0;
  int outer_glow_layers = 0;
  int bevel_layers = 0;
  for (const auto& layer : document.layers()) {
    if (!layer.layer_style().empty()) {
      ++styled_layers;
    }
    if (!layer.layer_style().gradient_fills.empty()) {
      ++gradient_layers;
    }
    if (!layer.layer_style().drop_shadows.empty()) {
      ++shadow_layers;
    }
    if (!layer.layer_style().outer_glows.empty()) {
      ++outer_glow_layers;
    }
    if (!layer.layer_style().bevels.empty()) {
      ++bevel_layers;
    }
  }
  CHECK(styled_layers >= 10);
  CHECK(gradient_layers >= 5);
  CHECK(shadow_layers >= 5);
  CHECK(outer_glow_layers >= 1);
  CHECK(bevel_layers >= 1);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  std::size_t visible_samples = 0;
  for (std::int32_t y = 0; y < flattened.height(); y += 16) {
    for (std::int32_t x = 0; x < flattened.width(); x += 16) {
      const auto* px = flattened.pixel(x, y);
      if (px[0] != 0 || px[1] != 0 || px[2] != 0) {
        ++visible_samples;
      }
    }
  }
  CHECK(visible_samples > 100);
  write_bmp_artifact("psd_title_screen_demo_layer_styles", document);
}

void psd_duke_nukem_mobile_text_style_renders_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("Duke nukem mobile.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  const auto document = patchy::psd::DocumentIo::read_file(path);
  CHECK(document.width() == 2480);
  CHECK(document.height() == 3508);

  bool found_large_text_style = false;
  const std::function<void(const std::vector<patchy::Layer>&)> visit = [&](const std::vector<patchy::Layer>& layers) {
    for (const auto& layer : layers) {
      if (layer.kind() == patchy::LayerKind::Group) {
        visit(layer.children());
        continue;
      }
      const auto& style = layer.layer_style();
      if (!patchy::layer_is_text(layer) || style.drop_shadows.empty() || style.outer_glows.empty()) {
        continue;
      }
      const auto has_large_shadow =
          std::any_of(style.drop_shadows.begin(), style.drop_shadows.end(), [](const patchy::LayerDropShadow& shadow) {
            return shadow.enabled && shadow.size >= 200.0F && shadow.spread >= 70.0F;
          });
      const auto has_large_glow =
          std::any_of(style.outer_glows.begin(), style.outer_glows.end(), [](const patchy::LayerOuterGlow& glow) {
            return glow.enabled && glow.size >= 150.0F && glow.spread >= 99.0F;
          });
      if (has_large_shadow && has_large_glow) {
        found_large_text_style = true;
        CHECK(layer.metadata().at(patchy::kLayerMetadataTextRasterStatus) == "patchy_raster");
        int tall_alpha_columns = 0;
        for (std::int32_t x = 0; x < layer.pixels().width(); ++x) {
          int column_alpha = 0;
          for (std::int32_t y = 0; y < layer.pixels().height(); ++y) {
            if (layer.pixels().pixel(x, y)[3] > 0U) {
              ++column_alpha;
            }
          }
          if (column_alpha * 4 > layer.pixels().height() * 3) {
            ++tall_alpha_columns;
          }
        }
        CHECK(tall_alpha_columns == 0);
      }
    }
  };
  visit(document.layers());
  CHECK(found_large_text_style);

  write_bmp_artifact("psd_duke_nukem_mobile_text_style", document);
}

void tool_brush_draws_color_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  const auto dirty = patchy::paint_brush(document, layer_id, 20, 20, tool_options(12, 140, 240), false);
  CHECK(!dirty.empty());
  const auto* px = document.find_layer(layer_id)->pixels().pixel(20, 20);
  CHECK(px[0] == 12);
  CHECK(px[1] == 140);
  CHECK(px[2] == 240);
  CHECK(px[3] == 255);
  write_bmp_artifact("tool_brush", document);
}

void tool_brush_opacity_and_bounded_layer_expansion_work() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 48, 255, 255, 255));
  patchy::Layer pasted(document.allocate_layer_id(), "Pasted", solid_rgba(8, 8, 0, 0, 0, 0));
  pasted.pixels().pixel(0, 0)[0] = 30;
  pasted.pixels().pixel(0, 0)[1] = 40;
  pasted.pixels().pixel(0, 0)[2] = 50;
  pasted.pixels().pixel(0, 0)[3] = 255;
  pasted.set_bounds(patchy::Rect{10, 10, 8, 8});
  const auto layer_id = pasted.id();
  document.add_layer(std::move(pasted));

  auto options = tool_options(240, 20, 30);
  options.brush_size = 5;
  options.primary.a = 128;
  const auto dirty = patchy::paint_brush(document, layer_id, 44, 32, options, false);
  CHECK(!dirty.empty());
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  CHECK(layer->bounds().contains(44, 32));
  CHECK(layer->bounds().contains(10, 10));
  CHECK(layer->pixels().pixel(10 - layer->bounds().x, 10 - layer->bounds().y)[3] == 255);
  const auto* painted = layer->pixels().pixel(44 - layer->bounds().x, 32 - layer->bounds().y);
  CHECK(painted[0] == 240);
  CHECK(painted[1] == 20);
  CHECK(painted[2] == 30);
  CHECK(painted[3] >= 120);
  CHECK(painted[3] <= 136);
  write_bmp_artifact("tool_brush_expand_layer", document);
}

void tool_brush_softness_feathers_edge_alpha() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(30, 90, 240);
  options.brush_size = 21;
  options.brush_softness = 100;

  const auto dirty = patchy::paint_brush(document, layer_id, 24, 24, options, false);
  CHECK(!dirty.empty());
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  const auto& pixels = layer->pixels();
  const auto* center = pixels.pixel(24, 24);
  const auto* feather = pixels.pixel(32, 24);
  const auto* outside = pixels.pixel(35, 24);
  CHECK(center[0] == 30);
  CHECK(center[1] == 90);
  CHECK(center[2] == 240);
  CHECK(center[3] == 255);
  CHECK(feather[0] == 30);
  CHECK(feather[1] == 90);
  CHECK(feather[2] == 240);
  CHECK(feather[3] > 0);
  CHECK(feather[3] < 255);
  CHECK(outside[3] == 0);
  write_bmp_artifact("tool_soft_brush", document);
}

void tool_brush_repaints_translucent_pixels_without_color_halo() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);

  auto black = tool_options(0, 0, 0);
  black.brush_size = 1;
  black.primary.a = 128;
  CHECK(!patchy::paint_brush(document, layer_id, 20, 20, black, false).empty());

  auto red = tool_options(255, 0, 0);
  red.brush_size = 1;
  red.primary.a = 128;
  CHECK(!patchy::paint_brush(document, layer_id, 20, 20, red, false).empty());

  const auto* px = document.find_layer(layer_id)->pixels().pixel(20, 20);
  CHECK(px[0] >= 165);
  CHECK(px[0] <= 176);
  CHECK(px[1] == 0);
  CHECK(px[2] == 0);
  CHECK(px[3] >= 190);
  CHECK(px[3] <= 194);
}

patchy::BrushTip make_bar_brush_tip() {
  // 9x9 tip with a single opaque horizontal bar through the middle row.
  patchy::BrushTip tip;
  tip.width = 9;
  tip.height = 9;
  tip.mask.assign(81, 0);
  for (std::int32_t x = 0; x < 9; ++x) {
    tip.mask[4U * 9U + static_cast<std::size_t>(x)] = 255;
  }
  return tip;
}

void brush_tip_mips_and_scaling_preserve_shape() {
  patchy::BrushTip tip;
  tip.width = 64;
  tip.height = 32;
  tip.mask.assign(static_cast<std::size_t>(64 * 32), 0);
  for (std::int32_t y = 8; y < 24; ++y) {
    for (std::int32_t x = 16; x < 48; ++x) {
      tip.mask[static_cast<std::size_t>(y) * 64U + static_cast<std::size_t>(x)] = 255;
    }
  }

  const auto mips = patchy::build_brush_tip_mips(tip);
  CHECK(!mips.empty());
  CHECK(mips.levels.size() >= 5);
  CHECK(mips.levels[1].width == 32);
  CHECK(mips.levels[1].height == 16);

  const auto scaled = patchy::make_scaled_brush_tip(mips, 16);
  CHECK(scaled.width == 16);
  CHECK(scaled.height == 8);
  CHECK(scaled.anchor_x == 8.0);
  CHECK(scaled.anchor_y == 4.0);
  // Center of the inner rectangle stays opaque; far corners stay empty.
  CHECK(scaled.mask[4U * 16U + 8U] == 255);
  CHECK(scaled.mask[0] == 0);
  CHECK(scaled.mask[scaled.mask.size() - 1U] == 0);

  const auto upscaled = patchy::make_scaled_brush_tip(mips, 128);
  CHECK(upscaled.width == 128);
  CHECK(upscaled.height == 64);
  CHECK(upscaled.mask[32U * 128U + 64U] == 255);
}

void tool_brush_tip_stamps_bitmap_mask() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);

  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  auto options = tool_options(10, 200, 60);
  options.brush_size = 9;
  options.brush_tip = &scaled;
  const auto dirty = patchy::paint_brush(document, layer_id, 24, 24, options, false);
  CHECK(!dirty.empty());

  const auto& pixels = document.find_layer(layer_id)->pixels();
  CHECK(pixels.pixel(24, 24)[3] == 255);  // bar center
  CHECK(pixels.pixel(28, 24)[3] == 255);  // bar end
  CHECK(pixels.pixel(24, 28)[3] == 0);    // above/below the bar stays empty
  CHECK(pixels.pixel(24, 22)[3] == 0);
  CHECK(pixels.pixel(24, 24)[0] == 10);
  CHECK(pixels.pixel(24, 24)[1] == 200);
  CHECK(pixels.pixel(24, 24)[2] == 60);
}

void tool_brush_tip_rotation_and_roundness_transform_stamp() {
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(0, 0, 0);
  options.brush_size = 9;
  options.brush_tip = &scaled;
  options.brush_angle_degrees = 90.0;
  CHECK(!patchy::paint_brush(document, layer_id, 24, 24, options, false).empty());
  const auto& rotated = document.find_layer(layer_id)->pixels();
  CHECK(rotated.pixel(24, 28)[3] == 255);  // bar now vertical
  CHECK(rotated.pixel(28, 24)[3] == 0);

  auto squashed_document = make_tool_document();
  const auto squashed_layer = active_tool_layer(squashed_document);
  auto squash_options = tool_options(0, 0, 0);
  squash_options.brush_size = 9;
  squash_options.brush_tip = &scaled;
  squash_options.brush_roundness = 50;  // halves the tip height; the bar itself stays a bar
  CHECK(!patchy::paint_brush(squashed_document, squashed_layer, 24, 24, squash_options, false).empty());
  const auto& squashed = squashed_document.find_layer(squashed_layer)->pixels();
  CHECK(squashed.pixel(24, 24)[3] == 255);
  CHECK(squashed.pixel(28, 24)[3] == 255);
  CHECK(squashed.pixel(24, 27)[3] == 0);
}

void tool_brush_tip_segment_spacing_carries_across_segments() {
  const auto square = [] {
    patchy::BrushTip tip;
    tip.width = 2;
    tip.height = 2;
    tip.mask.assign(4, 255);
    return tip;
  }();
  const auto mips = patchy::build_brush_tip_mips(square);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 4);

  auto options = tool_options(0, 0, 0);
  options.brush_size = 4;
  options.brush_tip = &scaled;
  options.brush_tip_spacing = 2.0;  // dabs every 8px: sparse enough to count

  const auto painted_alpha_pixels = [](const patchy::Document& document, patchy::LayerId layer_id) {
    const auto& pixels = document.find_layer(layer_id)->pixels();
    int count = 0;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        if (pixels.pixel(x, y)[3] > 0U) {
          ++count;
        }
      }
    }
    return count;
  };

  // One long segment...
  auto whole_document = make_tool_document();
  const auto whole_layer = active_tool_layer(whole_document);
  patchy::BrushTipStrokeState whole_state;
  CHECK(!patchy::paint_brush_segment(whole_document, whole_layer, 10.0, 20.0, 25.0, 20.0, options, false,
                                     whole_state)
             .empty());

  // ...must paint the same pixels as the same path chopped into short segments (the canvas
  // stroke smoother emits steps shorter than the dab spacing).
  auto chopped_document = make_tool_document();
  const auto chopped_layer = active_tool_layer(chopped_document);
  patchy::BrushTipStrokeState chopped_state;
  auto chopped_dirty = patchy::paint_brush_segment(chopped_document, chopped_layer, 10.0, 20.0, 15.0, 20.0,
                                                   options, false, chopped_state);
  chopped_dirty = patchy::unite_rect(
      chopped_dirty, patchy::paint_brush_segment(chopped_document, chopped_layer, 15.0, 20.0, 20.0, 20.0, options,
                                                 false, chopped_state));
  chopped_dirty = patchy::unite_rect(
      chopped_dirty, patchy::paint_brush_segment(chopped_document, chopped_layer, 20.0, 20.0, 25.0, 20.0, options,
                                                 false, chopped_state));
  CHECK(!chopped_dirty.empty());

  const auto whole_count = painted_alpha_pixels(whole_document, whole_layer);
  const auto chopped_count = painted_alpha_pixels(chopped_document, chopped_layer);
  CHECK(whole_count == chopped_count);
  // 15px path, spacing 8: a dab at the start plus one 8px in. Each 4x4 stamp antialiases into a
  // 5x5 footprint, and the two footprints are disjoint.
  CHECK(whole_count == 50);
  const auto& whole_pixels = whole_document.find_layer(whole_layer)->pixels();
  const auto& chopped_pixels = chopped_document.find_layer(chopped_layer)->pixels();
  for (std::int32_t y = 16; y < 24; ++y) {
    for (std::int32_t x = 6; x < 30; ++x) {
      CHECK(whole_pixels.pixel(x, y)[3] == chopped_pixels.pixel(x, y)[3]);
    }
  }
}

void brush_dynamics_rng_sequence_is_stable() {
  // Freezes the splitmix64 stream: dynamics tests assert exact pixels, so the generator must
  // never change behavior (and std::uniform_* distributions must never sneak in).
  patchy::BrushDynamicsRng rng;
  rng.seed(42);
  CHECK(rng.next_u64() == 18047550643177690414ULL);
  CHECK(rng.next_u64() == 13520925650152286267ULL);
  CHECK(rng.next_u64() == 11490439774882940890ULL);
  CHECK(rng.next_u64() == 11819688046890619624ULL);

  rng.seed(42);
  CHECK(rng.next_unit() == 0.97835968076877067);
  CHECK(rng.next_unit() == 0.73297084819550451);

  rng.seed(0);
  CHECK(rng.next_u64() == 7960286522194355700ULL);

  rng.seed(7);
  for (int i = 0; i < 100; ++i) {
    const auto value = rng.next_signed_unit();
    CHECK(value >= -1.0);
    CHECK(value < 1.0);
  }

  rng.seed(3);
  bool saw_true = false;
  bool saw_false = false;
  for (int i = 0; i < 64 && !(saw_true && saw_false); ++i) {
    if (rng.next_bool()) {
      saw_true = true;
    } else {
      saw_false = true;
    }
  }
  CHECK(saw_true);
  CHECK(saw_false);
}

void brush_dynamics_activation_gates_fields() {
  patchy::BrushDynamics dynamics;
  CHECK(!dynamics.active());

  // Floors, axis flags, fade length, and the per-stroke inputs never activate on their own.
  dynamics.minimum_diameter = 0.5;
  dynamics.minimum_roundness = 0.5;
  dynamics.scatter_both_axes = true;
  dynamics.count_jitter = 1.0;
  dynamics.angle_fade_steps = 5;
  dynamics.seed = 77;
  dynamics.pen_pressure = 0.5;
  dynamics.pen_tilt = 0.25;
  dynamics.pen_wheel = 0.75;
  dynamics.pen_tilt_azimuth_degrees = 45.0;
  dynamics.pen_tilt_azimuth_valid = true;
  dynamics.pen_rotation_degrees = 12.0;
  dynamics.pen_rotation_valid = true;
  CHECK(!dynamics.active());

  // Controls of Off / GlobalDefault only steer global-preference precedence; they must not
  // flip the per-dab path on (a Round brush that merely disables pressure stays procedural).
  dynamics.size_control = patchy::BrushDynamicControl::Off;
  dynamics.roundness_control = patchy::BrushDynamicControl::Off;
  dynamics.opacity_control = patchy::BrushDynamicControl::Off;
  dynamics.size_fade_steps = 7;
  dynamics.minimum_opacity = 0.4;
  CHECK(!dynamics.active());

  CHECK(patchy::BrushDynamics{.size_jitter = 0.1}.active());
  CHECK(patchy::BrushDynamics{.angle_jitter = 0.1}.active());
  CHECK(patchy::BrushDynamics{.angle_control = patchy::BrushDynamicControl::Direction}.active());
  CHECK(patchy::BrushDynamics{.roundness_jitter = 0.1}.active());
  CHECK(patchy::BrushDynamics{.flip_x_jitter = true}.active());
  CHECK(patchy::BrushDynamics{.flip_y_jitter = true}.active());
  CHECK(patchy::BrushDynamics{.scatter = 0.5}.active());
  CHECK(patchy::BrushDynamics{.count = 2}.active());
  CHECK(patchy::BrushDynamics{.opacity_jitter = 0.1}.active());
  // A control with a real source activates even with zero jitter...
  CHECK(patchy::BrushDynamics{.size_control = patchy::BrushDynamicControl::PenPressure}.active());
  CHECK(patchy::BrushDynamics{.roundness_control = patchy::BrushDynamicControl::Fade}.active());
  CHECK(patchy::BrushDynamics{.opacity_control = patchy::BrushDynamicControl::StylusWheel}.active());
  // ...but scatter/count controls are inert until scatter/count themselves are non-default.
  CHECK(!patchy::BrushDynamics{.scatter_control = patchy::BrushDynamicControl::PenPressure}.active());
  CHECK(!patchy::BrushDynamics{.count_control = patchy::BrushDynamicControl::Fade}.active());
}

void brush_dynamics_control_values() {
  // Per-dynamic controls map their input into [minimum, 1] deterministically (no RNG draws),
  // Photoshop-style: pressure/tilt/wheel read directly, rotation wraps into 0..1, fade uses the
  // per-dynamic step count, and a missing input reads as full value (mouse strokes).
  const auto approx = [](double value, double expected) { return std::abs(value - expected) < 1e-9; };
  patchy::BrushDynamicsRng rng;
  patchy::BrushDynamicsStrokeContext context;

  patchy::BrushDynamics size;
  size.size_control = patchy::BrushDynamicControl::PenPressure;
  size.minimum_diameter = 0.2;
  size.pen_pressure = 0.0;
  rng.seed(1);
  CHECK(approx(patchy::sample_dab_variation(size, rng, context, 100).scale, 0.2));
  size.pen_pressure = 0.5;
  CHECK(approx(patchy::sample_dab_variation(size, rng, context, 100).scale, 0.6));
  size.pen_pressure = 1.0;
  CHECK(approx(patchy::sample_dab_variation(size, rng, context, 100).scale, 1.0));

  // Defaulted pen inputs (no pen) read as full value, not zero.
  patchy::BrushDynamics mouse;
  mouse.size_control = patchy::BrushDynamicControl::PenPressure;
  CHECK(approx(patchy::sample_dab_variation(mouse, rng, context, 100).scale, 1.0));

  patchy::BrushDynamics roundness;
  roundness.roundness_control = patchy::BrushDynamicControl::PenTilt;
  roundness.minimum_roundness = 0.4;
  roundness.pen_tilt = 0.5;
  CHECK(approx(patchy::sample_dab_variation(roundness, rng, context, 100).roundness_multiplier, 0.7));

  patchy::BrushDynamics opacity;
  opacity.opacity_control = patchy::BrushDynamicControl::StylusWheel;
  opacity.minimum_opacity = 0.2;
  opacity.pen_wheel = 0.25;
  CHECK(approx(patchy::sample_dab_variation(opacity, rng, context, 100).opacity_multiplier, 0.4));
  opacity.opacity_control = patchy::BrushDynamicControl::PenRotation;
  opacity.pen_rotation_degrees = -90.0;  // wraps to 270deg = 0.75
  opacity.pen_rotation_valid = true;
  CHECK(approx(patchy::sample_dab_variation(opacity, rng, context, 100).opacity_multiplier, 0.8));
  opacity.pen_rotation_valid = false;  // no rotation hardware: full value
  CHECK(approx(patchy::sample_dab_variation(opacity, rng, context, 100).opacity_multiplier, 1.0));

  // Fade scales scatter offsets by the per-dynamic step curve: identical RNG draws, half range.
  patchy::BrushDynamics scatter;
  scatter.scatter = 1.0;
  scatter.scatter_control = patchy::BrushDynamicControl::Fade;
  scatter.scatter_fade_steps = 10;
  context.step_index = 0;
  rng.seed(9);
  const auto full_offset = patchy::sample_dab_variation(scatter, rng, context, 100).offset_y;
  context.step_index = 5;
  rng.seed(9);
  const auto faded_offset = patchy::sample_dab_variation(scatter, rng, context, 100).offset_y;
  CHECK(std::abs(full_offset) > 1.0);
  CHECK(approx(faded_offset, full_offset * 0.5));
  context.step_index = 0;

  // Count control fades the dab count toward 1; the count-jitter draw gate is untouched.
  patchy::BrushDynamics count;
  count.count = 5;
  count.count_control = patchy::BrushDynamicControl::PenPressure;
  count.pen_pressure = 1.0;
  CHECK(patchy::sample_dab_count(count, rng, context) == 5);
  count.pen_pressure = 0.5;
  CHECK(patchy::sample_dab_count(count, rng, context) == 3);
  count.pen_pressure = 0.0;
  CHECK(patchy::sample_dab_count(count, rng, context) == 1);
  count.count_jitter = 1.0;
  rng.seed(4);
  const auto jittered = patchy::sample_dab_count(count, rng, context);
  CHECK(jittered >= 1 && jittered <= 5);
}

void tool_brush_tip_controls_at_full_value_change_nothing() {
  // Controls modulate results without consuming randomness, so sourcing every control while its
  // input sits at full value must reproduce the jitter-only stroke pixel for pixel (this pins
  // the RNG draw-order contract across the control feature).
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  patchy::BrushDynamics jitter_only;
  jitter_only.size_jitter = 0.5;
  jitter_only.minimum_diameter = 0.3;
  jitter_only.angle_jitter = 0.4;
  jitter_only.roundness_jitter = 0.5;
  jitter_only.scatter = 0.8;
  jitter_only.scatter_both_axes = true;
  jitter_only.count = 3;
  jitter_only.count_jitter = 0.5;
  jitter_only.opacity_jitter = 0.4;
  jitter_only.seed = 4242;

  auto with_controls = jitter_only;
  with_controls.size_control = patchy::BrushDynamicControl::PenPressure;
  with_controls.roundness_control = patchy::BrushDynamicControl::StylusWheel;
  with_controls.scatter_control = patchy::BrushDynamicControl::PenPressure;
  with_controls.count_control = patchy::BrushDynamicControl::StylusWheel;
  with_controls.opacity_control = patchy::BrushDynamicControl::PenTilt;
  with_controls.minimum_opacity = 0.3;  // irrelevant at full input value
  // All pen inputs stay at their 1.0 defaults = full value.

  const auto paint_stroke = [&scaled](patchy::Document& document, patchy::LayerId layer,
                                      const patchy::BrushDynamics& dynamics) {
    auto options = tool_options(0, 0, 0);
    options.brush_size = 9;
    options.brush_tip = &scaled;
    options.brush_tip_spacing = 0.5;
    options.brush_dynamics = dynamics;
    patchy::BrushTipStrokeState state;
    return patchy::paint_brush_segment(document, layer, 10.0, 20.0, 44.0, 30.0, options, false, state);
  };

  auto plain_document = make_tool_document();
  const auto plain_layer = active_tool_layer(plain_document);
  CHECK(!paint_stroke(plain_document, plain_layer, jitter_only).empty());
  auto controlled_document = make_tool_document();
  const auto controlled_layer = active_tool_layer(controlled_document);
  CHECK(!paint_stroke(controlled_document, controlled_layer, with_controls).empty());

  const auto& plain = plain_document.find_layer(plain_layer)->pixels();
  const auto& controlled = controlled_document.find_layer(controlled_layer)->pixels();
  for (std::int32_t y = 0; y < plain.height(); ++y) {
    for (std::int32_t x = 0; x < plain.width(); ++x) {
      CHECK(plain.pixel(x, y)[3] == controlled.pixel(x, y)[3]);
    }
  }
}

void tool_brush_tip_size_control_pressure_scales_dabs() {
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  const auto stroke_extent = [&scaled](double pressure) {
    auto options = tool_options(0, 0, 0);
    options.brush_size = 9;
    options.brush_tip = &scaled;
    options.brush_tip_spacing = 2.0;  // dabs at x=10 and x=28
    options.brush_dynamics.size_control = patchy::BrushDynamicControl::PenPressure;
    options.brush_dynamics.minimum_diameter = 0.5;
    options.brush_dynamics.pen_pressure = pressure;
    options.brush_dynamics.seed = 77;
    auto document = make_tool_document();
    const auto layer = active_tool_layer(document);
    patchy::BrushTipStrokeState state;
    CHECK(!patchy::paint_brush_segment(document, layer, 10.0, 20.0, 30.0, 20.0, options, false, state).empty());
    const auto& pixels = document.find_layer(layer)->pixels();
    std::int32_t min_x = 1000;
    std::int32_t max_x = -1000;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 2; x <= std::min(pixels.width() - 1, 18); ++x) {
        if (pixels.pixel(x, y)[3] > 0U) {
          min_x = std::min(min_x, x);
          max_x = std::max(max_x, x);
        }
      }
    }
    CHECK(max_x >= min_x);
    return max_x - min_x + 1;
  };

  // Zero pressure sits on the minimum-diameter floor, full pressure paints the full stamp, and
  // the mapping is monotonic in between (no randomness is involved with zero jitter).
  const auto light = stroke_extent(0.0);
  const auto medium = stroke_extent(0.5);
  const auto full = stroke_extent(1.0);
  CHECK(light >= 3 && light <= 6);  // 0.5 x 9px, antialiased
  CHECK(medium > light);
  CHECK(full > medium);
  CHECK(full >= 9);
}

void tool_brush_tip_opacity_control_respects_minimum() {
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  const auto dab_alpha = [&scaled](double pressure) {
    auto options = tool_options(0, 0, 0);
    options.brush_size = 9;
    options.brush_tip = &scaled;
    options.brush_tip_spacing = 2.0;
    options.brush_dynamics.opacity_control = patchy::BrushDynamicControl::PenPressure;
    options.brush_dynamics.minimum_opacity = 0.3;
    options.brush_dynamics.pen_pressure = pressure;
    options.brush_dynamics.seed = 5;
    auto document = make_tool_document();
    const auto layer = active_tool_layer(document);
    patchy::BrushTipStrokeState state;
    CHECK(!patchy::paint_brush_segment(document, layer, 10.0, 20.0, 10.0, 20.0, options, false, state).empty());
    const auto& pixels = document.find_layer(layer)->pixels();
    std::uint8_t max_alpha = 0;
    for (std::int32_t x = 5; x <= 15; ++x) {
      max_alpha = std::max(max_alpha, pixels.pixel(x, 20)[3]);
    }
    return static_cast<int>(max_alpha);
  };

  // Pressure 0 bottoms out at the Minimum Opacity floor (30% of 255 with a little AA slack),
  // pressure 1 paints at full strength.
  const auto floor_alpha = dab_alpha(0.0);
  const auto full_alpha = dab_alpha(1.0);
  CHECK(floor_alpha >= 66 && floor_alpha <= 88);
  CHECK(full_alpha >= 250);
}

void tool_brush_tip_inactive_dynamics_change_nothing() {
  // An inactive BrushDynamics (even with a seed and pen inputs set) must leave the stamp path
  // on the exact historical code: pixel-for-pixel identical strokes.
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  const auto paint_stroke = [&scaled](patchy::Document& document, patchy::LayerId layer,
                                      const patchy::BrushDynamics& dynamics) {
    auto options = tool_options(0, 0, 0);
    options.brush_size = 9;
    options.brush_tip = &scaled;
    options.brush_tip_spacing = 0.5;
    options.brush_dynamics = dynamics;
    patchy::BrushTipStrokeState state;
    return patchy::paint_brush_segment(document, layer, 10.0, 20.0, 40.0, 26.0, options, false, state);
  };

  auto plain_document = make_tool_document();
  const auto plain_layer = active_tool_layer(plain_document);
  CHECK(!paint_stroke(plain_document, plain_layer, patchy::BrushDynamics{}).empty());

  patchy::BrushDynamics inactive;
  inactive.seed = 99;
  inactive.pen_pressure = 0.25;
  inactive.pen_tilt_azimuth_degrees = 33.0;
  inactive.pen_tilt_azimuth_valid = true;
  inactive.size_control = patchy::BrushDynamicControl::Off;  // suppression-only, still inactive
  auto seeded_document = make_tool_document();
  const auto seeded_layer = active_tool_layer(seeded_document);
  CHECK(!paint_stroke(seeded_document, seeded_layer, inactive).empty());

  const auto& plain = plain_document.find_layer(plain_layer)->pixels();
  const auto& seeded = seeded_document.find_layer(seeded_layer)->pixels();
  for (std::int32_t y = 0; y < plain.height(); ++y) {
    for (std::int32_t x = 0; x < plain.width(); ++x) {
      CHECK(plain.pixel(x, y)[3] == seeded.pixel(x, y)[3]);
    }
  }
}

void tool_brush_tip_size_jitter_shrinks_dabs_deterministically() {
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  auto options = tool_options(0, 0, 0);
  options.brush_size = 9;
  options.brush_tip = &scaled;
  options.brush_tip_spacing = 2.0;  // dabs every 18px: (10,20) and (28,20)
  options.brush_dynamics.size_jitter = 1.0;
  options.brush_dynamics.minimum_diameter = 0.5;
  options.brush_dynamics.seed = 1234;

  const auto paint_stroke = [&options](patchy::Document& document, patchy::LayerId layer) {
    patchy::BrushTipStrokeState state;
    return patchy::paint_brush_segment(document, layer, 10.0, 20.0, 30.0, 20.0, options, false, state);
  };

  auto document = make_tool_document();
  const auto layer = active_tool_layer(document);
  CHECK(!paint_stroke(document, layer).empty());
  const auto& pixels = document.find_layer(layer)->pixels();

  // Horizontal extent of the painted bar around each dab center: full size ~9-10 px with
  // antialiasing, minimum diameter 0.5 floors it at ~4.
  const auto dab_extent = [&pixels](std::int32_t center_x) {
    std::int32_t min_x = 1000;
    std::int32_t max_x = -1000;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = std::max(0, center_x - 8); x <= std::min(pixels.width() - 1, center_x + 8); ++x) {
        if (pixels.pixel(x, y)[3] > 0U) {
          min_x = std::min(min_x, x);
          max_x = std::max(max_x, x);
        }
      }
    }
    CHECK(max_x >= min_x);
    return max_x - min_x + 1;
  };
  const auto first_extent = dab_extent(10);
  const auto second_extent = dab_extent(28);
  CHECK(first_extent <= 10);
  CHECK(second_extent <= 10);
  CHECK(first_extent >= 4);
  CHECK(second_extent >= 4);
  // Size jitter only shrinks; with jitter 1.0 the chosen seed visibly shrinks at least one dab.
  CHECK(std::min(first_extent, second_extent) <= 8);

  // Same seed reproduces the exact pixels; a different seed varies them.
  auto replay_document = make_tool_document();
  const auto replay_layer = active_tool_layer(replay_document);
  CHECK(!paint_stroke(replay_document, replay_layer).empty());
  const auto& replay = replay_document.find_layer(replay_layer)->pixels();
  bool replay_identical = true;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      replay_identical = replay_identical && pixels.pixel(x, y)[3] == replay.pixel(x, y)[3];
    }
  }
  CHECK(replay_identical);

  options.brush_dynamics.seed = 5678;
  auto reseeded_document = make_tool_document();
  const auto reseeded_layer = active_tool_layer(reseeded_document);
  CHECK(!paint_stroke(reseeded_document, reseeded_layer).empty());
  const auto& reseeded = reseeded_document.find_layer(reseeded_layer)->pixels();
  bool reseeded_identical = true;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      reseeded_identical = reseeded_identical && pixels.pixel(x, y)[3] == reseeded.pixel(x, y)[3];
    }
  }
  CHECK(!reseeded_identical);
}

void tool_brush_tip_angle_direction_follows_stroke() {
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  const auto base_options = [&scaled] {
    auto options = tool_options(0, 0, 0);
    options.brush_size = 9;
    options.brush_tip = &scaled;
    options.brush_tip_spacing = 2.0;  // dabs every 18px
    return options;
  };

  // Direction control: a vertical stroke rotates the horizontal bar to vertical, including the
  // very first dab (the segment direction is latched before it stamps).
  auto vertical_document = make_tool_document();
  const auto vertical_layer = active_tool_layer(vertical_document);
  auto vertical_options = base_options();
  vertical_options.brush_dynamics.angle_control = patchy::BrushDynamicControl::Direction;
  patchy::BrushTipStrokeState vertical_state;
  CHECK(!patchy::paint_brush_segment(vertical_document, vertical_layer, 24.0, 10.0, 24.0, 30.0,
                                     vertical_options, false, vertical_state)
             .empty());
  const auto& vertical = vertical_document.find_layer(vertical_layer)->pixels();
  CHECK(vertical.pixel(24, 13)[3] == 255);  // dab at (24,10) spans y 6..14
  CHECK(vertical.pixel(27, 10)[3] == 0);
  CHECK(vertical.pixel(24, 25)[3] == 255);  // dab at (24,28) spans y 24..32
  CHECK(vertical.pixel(27, 28)[3] == 0);

  // A horizontal stroke keeps the bar horizontal (direction angle 0).
  auto horizontal_document = make_tool_document();
  const auto horizontal_layer = active_tool_layer(horizontal_document);
  patchy::BrushTipStrokeState horizontal_state;
  CHECK(!patchy::paint_brush_segment(horizontal_document, horizontal_layer, 10.0, 24.0, 30.0, 24.0,
                                     vertical_options, false, horizontal_state)
             .empty());
  const auto& horizontal = horizontal_document.find_layer(horizontal_layer)->pixels();
  CHECK(horizontal.pixel(13, 24)[3] == 255);
  CHECK(horizontal.pixel(10, 27)[3] == 0);

  // InitialDirection latches the first leg's angle: dabs on the descending leg stay horizontal.
  auto initial_document = make_tool_document();
  const auto initial_layer = active_tool_layer(initial_document);
  auto initial_options = base_options();
  initial_options.brush_dynamics.angle_control = patchy::BrushDynamicControl::InitialDirection;
  patchy::BrushTipStrokeState initial_state;
  CHECK(!patchy::paint_brush_segment(initial_document, initial_layer, 10.0, 20.0, 26.0, 20.0,
                                     initial_options, false, initial_state)
             .empty());
  CHECK(!patchy::paint_brush_segment(initial_document, initial_layer, 26.0, 20.0, 26.0, 40.0,
                                     initial_options, false, initial_state)
             .empty());
  const auto& initial = initial_document.find_layer(initial_layer)->pixels();
  CHECK(initial.pixel(29, 22)[3] == 255);  // dab at (26,22) on the vertical leg, still horizontal
  CHECK(initial.pixel(26, 25)[3] == 0);
  CHECK(initial.pixel(29, 40)[3] == 255);  // dab at (26,40)
  CHECK(initial.pixel(26, 43)[3] == 0);
}

void tool_brush_tip_angle_fade_and_jitter_rotate_dabs() {
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  // Fade sweeps the angle over fade_steps spacing steps: step 0 = 360° (horizontal bar),
  // step 1 with fade_steps 4 = 270° (vertical bar).
  auto fade_document = make_tool_document();
  const auto fade_layer = active_tool_layer(fade_document);
  auto fade_options = tool_options(0, 0, 0);
  fade_options.brush_size = 9;
  fade_options.brush_tip = &scaled;
  fade_options.brush_tip_spacing = 2.0;
  fade_options.brush_dynamics.angle_control = patchy::BrushDynamicControl::Fade;
  fade_options.brush_dynamics.angle_fade_steps = 4;
  patchy::BrushTipStrokeState fade_state;
  CHECK(!patchy::paint_brush_segment(fade_document, fade_layer, 10.0, 20.0, 30.0, 20.0, fade_options,
                                     false, fade_state)
             .empty());
  const auto& fade = fade_document.find_layer(fade_layer)->pixels();
  CHECK(fade.pixel(13, 20)[3] == 255);  // step 0: horizontal
  CHECK(fade.pixel(10, 23)[3] == 0);
  CHECK(fade.pixel(28, 23)[3] == 255);  // step 1: rotated to vertical
  CHECK(fade.pixel(31, 20)[3] == 0);

  // Angle jitter is seed-deterministic.
  const auto paint_jittered = [&scaled](patchy::Document& document, patchy::LayerId layer,
                                        std::uint32_t seed) {
    auto options = tool_options(0, 0, 0);
    options.brush_size = 9;
    options.brush_tip = &scaled;
    options.brush_tip_spacing = 2.0;
    options.brush_dynamics.angle_jitter = 1.0;
    options.brush_dynamics.seed = seed;
    patchy::BrushTipStrokeState state;
    return patchy::paint_brush_segment(document, layer, 10.0, 20.0, 30.0, 20.0, options, false, state);
  };
  auto first_document = make_tool_document();
  const auto first_layer = active_tool_layer(first_document);
  CHECK(!paint_jittered(first_document, first_layer, 777).empty());
  auto second_document = make_tool_document();
  const auto second_layer = active_tool_layer(second_document);
  CHECK(!paint_jittered(second_document, second_layer, 777).empty());
  auto third_document = make_tool_document();
  const auto third_layer = active_tool_layer(third_document);
  CHECK(!paint_jittered(third_document, third_layer, 778).empty());

  const auto& first = first_document.find_layer(first_layer)->pixels();
  const auto& second = second_document.find_layer(second_layer)->pixels();
  const auto& third = third_document.find_layer(third_layer)->pixels();
  bool same_seed_identical = true;
  bool other_seed_identical = true;
  for (std::int32_t y = 0; y < first.height(); ++y) {
    for (std::int32_t x = 0; x < first.width(); ++x) {
      same_seed_identical = same_seed_identical && first.pixel(x, y)[3] == second.pixel(x, y)[3];
      other_seed_identical = other_seed_identical && first.pixel(x, y)[3] == third.pixel(x, y)[3];
    }
  }
  CHECK(same_seed_identical);
  CHECK(!other_seed_identical);
}

void tool_brush_tip_flip_jitter_mirrors_stamp() {
  // A tip with a bar only in its left half: unflipped dabs paint left of center, flip-X dabs
  // paint right of center. Across several seeded clicks both orientations must appear.
  patchy::BrushTip half_bar;
  half_bar.width = 9;
  half_bar.height = 9;
  half_bar.mask.assign(81, 0);
  for (std::int32_t x = 0; x < 4; ++x) {
    half_bar.mask[4U * 9U + static_cast<std::size_t>(x)] = 255;
  }
  const auto mips = patchy::build_brush_tip_mips(half_bar);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  auto document = make_tool_document();
  const auto layer = active_tool_layer(document);
  bool saw_left = false;
  bool saw_right = false;
  for (std::uint32_t click = 0; click < 11; ++click) {
    auto options = tool_options(0, 0, 0);
    options.brush_size = 9;
    options.brush_tip = &scaled;
    options.brush_dynamics.flip_x_jitter = true;
    options.brush_dynamics.seed = click;
    patchy::BrushTipStrokeState state;
    const auto y = 4.0 + 4.0 * static_cast<double>(click);
    CHECK(!patchy::paint_brush_segment(document, layer, 24.0, y, 24.0, y, options, false, state).empty());

    const auto& pixels = document.find_layer(layer)->pixels();
    const auto row = static_cast<std::int32_t>(y);
    bool left = false;
    bool right = false;
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (pixels.pixel(x, row)[3] > 128U) {
        left = left || x <= 22;
        right = right || x >= 26;
      }
    }
    CHECK(left != right);  // each dab is either unflipped or mirrored, never both
    saw_left = saw_left || left;
    saw_right = saw_right || right;
  }
  CHECK(saw_left);
  CHECK(saw_right);
}

void tool_brush_tip_scatter_offsets_perpendicular_to_stroke() {
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  const auto painted_bounds = [](const patchy::PixelBuffer& pixels, std::int32_t& min_x,
                                 std::int32_t& max_x, std::int32_t& min_y, std::int32_t& max_y) {
    min_x = min_y = 1000;
    max_x = max_y = -1000;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        if (pixels.pixel(x, y)[3] > 0U) {
          min_x = std::min(min_x, x);
          max_x = std::max(max_x, x);
          min_y = std::min(min_y, y);
          max_y = std::max(max_y, y);
        }
      }
    }
  };

  // Scatter on a horizontal stroke moves dabs vertically only: the dab centers stay on the
  // spacing grid horizontally, so x stays within the bar footprint of the grid positions.
  auto document = make_tool_document();
  const auto layer = active_tool_layer(document);
  auto options = tool_options(0, 0, 0);
  options.brush_size = 9;
  options.brush_tip = &scaled;
  options.brush_tip_spacing = 2.0;  // dabs at x=10 and x=28
  options.brush_dynamics.scatter = 1.5;  // offsets up to 13.5px
  options.brush_dynamics.seed = 4321;    // draws +0.64 / -0.86: dabs land ~20px apart vertically
  patchy::BrushTipStrokeState state;
  CHECK(!patchy::paint_brush_segment(document, layer, 10.0, 20.0, 30.0, 20.0, options, false, state).empty());

  std::int32_t min_x = 0;
  std::int32_t max_x = 0;
  std::int32_t min_y = 0;
  std::int32_t max_y = 0;
  painted_bounds(document.find_layer(layer)->pixels(), min_x, max_x, min_y, max_y);
  CHECK(min_x >= 5);   // 10 - bar half width
  CHECK(max_x <= 33);  // 28 + bar half width
  CHECK(min_y >= 6);   // 20 - 13.5 - antialias
  CHECK(max_y <= 34);
  CHECK(max_y - min_y >= 4);  // the chosen seed visibly leaves the stroke line

  // Both Axes adds parallel offsets: painted pixels now stray outside the grid columns.
  auto both_document = make_tool_document();
  const auto both_layer = active_tool_layer(both_document);
  options.brush_dynamics.scatter_both_axes = true;
  patchy::BrushTipStrokeState both_state;
  CHECK(!patchy::paint_brush_segment(both_document, both_layer, 10.0, 20.0, 30.0, 20.0, options, false,
                                     both_state)
             .empty());
  painted_bounds(both_document.find_layer(both_layer)->pixels(), min_x, max_x, min_y, max_y);
  CHECK(min_x < 5 || max_x > 33);
}

void tool_brush_tip_count_stamps_multiple_dabs_per_step() {
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  const auto painted_pixels = [](const patchy::Document& document, patchy::LayerId layer_id) {
    const auto& pixels = document.find_layer(layer_id)->pixels();
    int count = 0;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        if (pixels.pixel(x, y)[3] > 0U) {
          ++count;
        }
      }
    }
    return count;
  };

  // A single click with count 3 and scatter stamps three scattered bars instead of one.
  auto single_document = make_tool_document();
  const auto single_layer = active_tool_layer(single_document);
  auto options = tool_options(0, 0, 0);
  options.brush_size = 9;
  options.brush_tip = &scaled;
  patchy::BrushTipStrokeState single_state;
  CHECK(!patchy::paint_brush_segment(single_document, single_layer, 24.0, 24.0, 24.0, 24.0, options, false,
                                     single_state)
             .empty());
  const auto single_count = painted_pixels(single_document, single_layer);

  auto counted_document = make_tool_document();
  const auto counted_layer = active_tool_layer(counted_document);
  options.brush_dynamics.count = 3;
  options.brush_dynamics.scatter = 1.0;
  options.brush_dynamics.seed = 555;
  patchy::BrushTipStrokeState counted_state;
  CHECK(!patchy::paint_brush_segment(counted_document, counted_layer, 24.0, 24.0, 24.0, 24.0, options,
                                     false, counted_state)
             .empty());
  const auto counted_count = painted_pixels(counted_document, counted_layer);
  CHECK(counted_count >= single_count * 2);  // three bars minus possible overlap

  // Count jitter stays within [1, count] and is seed-deterministic.
  options.brush_dynamics.count_jitter = 1.0;
  auto jittered_document = make_tool_document();
  const auto jittered_layer = active_tool_layer(jittered_document);
  patchy::BrushTipStrokeState jittered_state;
  CHECK(!patchy::paint_brush_segment(jittered_document, jittered_layer, 24.0, 24.0, 24.0, 24.0, options,
                                     false, jittered_state)
             .empty());
  const auto jittered_count = painted_pixels(jittered_document, jittered_layer);
  CHECK(jittered_count >= 8);   // at least one full bar
  CHECK(jittered_count <= 60);  // never more than `count` bars (3 × ~20px antialiased)

  // Heavier dynamics stay fast: count 8 with scatter across a long stroke.
  patchy::Document wide_document(512, 128, patchy::PixelFormat::rgb8());
  wide_document.add_pixel_layer("Background", solid_rgb(512, 128, 255, 255, 255));
  wide_document.add_pixel_layer("Paint", solid_rgba(512, 128, 0, 0, 0, 0));
  const auto wide_layer = active_tool_layer(wide_document);
  auto wide_options = tool_options(0, 0, 0);
  wide_options.brush_size = 64;
  const auto wide_mips = patchy::build_brush_tip_mips([&] {
    patchy::BrushTip block;
    block.width = 64;
    block.height = 64;
    block.mask.assign(64U * 64U, 255);
    return block;
  }());
  const auto wide_scaled = patchy::make_scaled_brush_tip(wide_mips, 64);
  wide_options.brush_tip = &wide_scaled;
  wide_options.brush_tip_spacing = 0.25;
  wide_options.brush_dynamics.count = 8;
  wide_options.brush_dynamics.scatter = 1.0;
  wide_options.brush_dynamics.size_jitter = 0.5;
  wide_options.brush_dynamics.angle_jitter = 1.0;
  wide_options.brush_dynamics.seed = 42;
  patchy::BrushTipStrokeState wide_state;
  const auto start = std::chrono::steady_clock::now();
  CHECK(!patchy::paint_brush_segment(wide_document, wide_layer, 40.0, 64.0, 460.0, 64.0, wide_options,
                                     false, wide_state)
             .empty());
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start)
                              .count();
  CHECK(elapsed_ms < 1000);
}

void tool_brush_tip_opacity_jitter_varies_dab_alpha() {
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  auto document = make_tool_document();
  const auto layer = active_tool_layer(document);
  auto options = tool_options(0, 0, 0);
  options.brush_size = 9;
  options.brush_tip = &scaled;
  options.brush_tip_spacing = 2.0;  // dabs at x=10, 28, 46
  options.brush_dynamics.opacity_jitter = 1.0;
  options.brush_dynamics.seed = 999;
  patchy::BrushTipStrokeState state;
  CHECK(!patchy::paint_brush_segment(document, layer, 10.0, 20.0, 48.0, 20.0, options, false, state).empty());

  const auto& pixels = document.find_layer(layer)->pixels();
  const auto dab_alpha = [&pixels](std::int32_t center_x) {
    std::uint8_t max_alpha = 0;
    for (std::int32_t x = std::max(0, center_x - 5); x <= std::min(pixels.width() - 1, center_x + 5); ++x) {
      max_alpha = std::max(max_alpha, pixels.pixel(x, 20)[3]);
    }
    return max_alpha;
  };
  const auto first = dab_alpha(10);
  const auto second = dab_alpha(28);
  const auto third = dab_alpha(46);
  CHECK(first >= 7);  // opacity floor is 3%
  CHECK(second >= 7);
  CHECK(third >= 7);
  CHECK(first <= 255);
  // The jitter must actually vary alpha across dabs for this seed.
  CHECK(!(first == second && second == third));
}

void tool_brush_tip_dynamics_carry_across_segments() {
  // With every dynamic active, one long segment and the same path chopped into short segments
  // must paint identical pixels: the RNG stream, fade step index, and stroke direction all live
  // in BrushTipStrokeState, not per-call.
  const auto tip = make_bar_brush_tip();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 9);

  auto options = tool_options(0, 0, 0);
  options.brush_size = 9;
  options.brush_tip = &scaled;
  options.brush_tip_spacing = 0.5;  // dabs every 4.5px
  options.brush_dynamics.size_jitter = 0.5;
  options.brush_dynamics.minimum_diameter = 0.3;
  options.brush_dynamics.angle_jitter = 0.5;
  options.brush_dynamics.angle_control = patchy::BrushDynamicControl::Direction;
  options.brush_dynamics.roundness_jitter = 0.4;
  options.brush_dynamics.flip_x_jitter = true;
  options.brush_dynamics.flip_y_jitter = true;
  options.brush_dynamics.scatter = 0.5;
  options.brush_dynamics.scatter_both_axes = true;
  options.brush_dynamics.count = 2;
  options.brush_dynamics.count_jitter = 0.5;
  options.brush_dynamics.opacity_jitter = 0.5;
  options.brush_dynamics.seed = 424242;

  auto whole_document = make_tool_document();
  const auto whole_layer = active_tool_layer(whole_document);
  patchy::BrushTipStrokeState whole_state;
  CHECK(!patchy::paint_brush_segment(whole_document, whole_layer, 10.0, 20.0, 25.0, 20.0, options, false,
                                     whole_state)
             .empty());

  auto chopped_document = make_tool_document();
  const auto chopped_layer = active_tool_layer(chopped_document);
  patchy::BrushTipStrokeState chopped_state;
  CHECK(!patchy::paint_brush_segment(chopped_document, chopped_layer, 10.0, 20.0, 15.0, 20.0, options,
                                     false, chopped_state)
             .empty());
  auto chopped_dirty = patchy::paint_brush_segment(chopped_document, chopped_layer, 15.0, 20.0, 20.0, 20.0,
                                                   options, false, chopped_state);
  chopped_dirty = patchy::unite_rect(
      chopped_dirty, patchy::paint_brush_segment(chopped_document, chopped_layer, 20.0, 20.0, 25.0, 20.0,
                                                 options, false, chopped_state));
  CHECK(!chopped_dirty.empty());

  const auto& whole_pixels = whole_document.find_layer(whole_layer)->pixels();
  const auto& chopped_pixels = chopped_document.find_layer(chopped_layer)->pixels();
  for (std::int32_t y = 0; y < whole_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < whole_pixels.width(); ++x) {
      CHECK(whole_pixels.pixel(x, y)[3] == chopped_pixels.pixel(x, y)[3]);
    }
  }
}

void brush_tip_softening_feathers_edges() {
  // A hard 16x16 block: after feathering, the stamp grows by the feather on each side, the
  // anchor shifts to match, and the edge becomes a gradient instead of a step.
  patchy::BrushTip tip;
  tip.width = 16;
  tip.height = 16;
  tip.mask.assign(256, 255);
  const auto mips = patchy::build_brush_tip_mips(tip);
  auto scaled = patchy::make_scaled_brush_tip(mips, 16);
  const auto hard_width = scaled.width;
  const auto hard_anchor = scaled.anchor_x;

  patchy::soften_scaled_brush_tip(scaled, 6);
  CHECK(scaled.width == hard_width + 12);
  CHECK(scaled.anchor_x == hard_anchor + 6.0);
  CHECK(scaled.mask.size() == static_cast<std::size_t>(scaled.width) * scaled.height);

  // Center stays essentially solid; the rim carries intermediate coverage.
  const auto at = [&scaled](int x, int y) {
    return scaled.mask[static_cast<std::size_t>(y) * scaled.width + x];
  };
  CHECK(at(scaled.width / 2, scaled.height / 2) > 240);
  int intermediate = 0;
  for (const auto value : scaled.mask) {
    if (value > 20 && value < 235) {
      ++intermediate;
    }
  }
  CHECK(intermediate > 100);

  // Zero feather is a no-op.
  auto untouched = patchy::make_scaled_brush_tip(mips, 16);
  patchy::soften_scaled_brush_tip(untouched, 0);
  CHECK(untouched.width == hard_width);
}

void tool_brush_tip_large_stamp_stroke_is_fast() {
  // A 500px tip painted across a wide canvas must stay interactive: 12 dabs at 25% spacing.
  patchy::BrushTip tip;
  tip.width = 500;
  tip.height = 500;
  tip.mask.assign(500U * 500U, 0);
  for (std::int32_t y = 0; y < 500; ++y) {
    for (std::int32_t x = 0; x < 500; ++x) {
      const auto dx = x - 250;
      const auto dy = y - 250;
      if (dx * dx + dy * dy <= 240 * 240) {
        tip.mask[static_cast<std::size_t>(y) * 500U + x] = 255;
      }
    }
  }

  patchy::Document document(1600, 1000, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(1600, 1000, 255, 255, 255));
  patchy::PixelBuffer pixels(1600, 1000, patchy::PixelFormat::rgba8());
  pixels.clear(0);
  const auto layer_id = document.add_pixel_layer("Paint", std::move(pixels)).id();

  const auto started = std::chrono::steady_clock::now();
  const auto mips = patchy::build_brush_tip_mips(tip);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 500);
  auto options = tool_options(30, 60, 200);
  options.brush_size = 500;
  options.brush_tip = &scaled;
  options.brush_tip_spacing = 0.25;
  patchy::BrushTipStrokeState state;
  const auto dirty =
      patchy::paint_brush_segment(document, layer_id, 250.0, 500.0, 1450.0, 500.0, options, false, state);
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();

  CHECK(!dirty.empty());
  CHECK(elapsed_ms < 1000);
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  const auto* center = layer->pixels().pixel(800 - layer->bounds().x, 500 - layer->bounds().y);
  CHECK(center[3] == 255);
  write_bmp_artifact("tool_brush_tip_large_stroke", document);
}

void tool_brush_tip_erases_and_respects_gates() {
  const auto square = [] {
    patchy::BrushTip tip;
    tip.width = 4;
    tip.height = 4;
    tip.mask.assign(16, 255);
    return tip;
  }();
  const auto mips = patchy::build_brush_tip_mips(square);
  const auto scaled = patchy::make_scaled_brush_tip(mips, 4);

  patchy::Document document(64, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 48, 255, 255, 255));
  const auto layer_id = document.add_pixel_layer("Paint", solid_rgba(64, 48, 10, 20, 30, 255)).id();

  auto options = tool_options(0, 0, 0);
  options.brush_size = 4;
  options.brush_tip = &scaled;
  options.primary.a = 255;
  CHECK(!patchy::paint_brush(document, layer_id, 20, 20, options, true).empty());
  const auto& pixels = document.find_layer(layer_id)->pixels();
  CHECK(pixels.pixel(20, 20)[3] == 0);   // erased under the stamp
  CHECK(pixels.pixel(26, 20)[3] == 255); // untouched outside

  // stroke_pixel_gate blocks the left half of a fresh stamp.
  auto gated_document = make_tool_document();
  const auto gated_layer = active_tool_layer(gated_document);
  auto gated_options = tool_options(200, 10, 10);
  gated_options.brush_size = 4;
  gated_options.brush_tip = &scaled;
  gated_options.stroke_pixel_gate = [](std::int32_t x, std::int32_t) { return x >= 24; };
  CHECK(!patchy::paint_brush(gated_document, gated_layer, 24, 24, gated_options, false).empty());
  const auto& gated_pixels = gated_document.find_layer(gated_layer)->pixels();
  CHECK(gated_pixels.pixel(23, 24)[3] == 0);
  CHECK(gated_pixels.pixel(24, 24)[3] == 255);
}

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  CHECK(static_cast<bool>(file));
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(file)), {});
}

void abr_v6_fixture_parses_brushes_names_and_spacing() {
  const auto bytes = read_binary_file(patchy::test::committed_abr_fixture_path("myer-settlement-brushes.abr"));
  std::string error;
  const auto result = patchy::psd::read_abr(bytes, error);
  CHECK(result.has_value());
  CHECK(error.empty());
  CHECK(result->warnings.empty());
  CHECK(result->brushes.size() == 148);

  const auto& first = result->brushes.front();
  CHECK(first.name == "Individual Tree 001");
  CHECK(first.width == 36);
  CHECK(first.height == 36);
  CHECK(first.spacing > 0.09 && first.spacing < 0.11);
  CHECK(first.mask.size() == 36U * 36U);
  std::uint64_t mask_sum = 0;
  for (const auto value : first.mask) {
    mask_sum += value;
  }
  CHECK(mask_sum == 81333U);

  CHECK(result->brushes.back().name == "Canons, Flags, & Guns");
  for (const auto& brush : result->brushes) {
    CHECK(brush.width > 0 && brush.width <= 4096);
    CHECK(brush.height > 0 && brush.height <= 4096);
    CHECK(brush.mask.size() == static_cast<std::size_t>(brush.width) * static_cast<std::size_t>(brush.height));
    CHECK(!brush.name.empty());
  }
}

void abr_dynamics_fixture_extracts_shape_and_scatter() {
  // photoshop-dynamics.abr was exported from Photoshop 2026 with every supported dynamic set to
  // a distinct value (see test-fixtures/abr/NOTICE.txt); this pins the descriptor key mapping.
  const auto approx = [](double value, double expected) { return std::abs(value - expected) < 1e-9; };

  const auto bytes = read_binary_file(patchy::test::committed_abr_fixture_path("photoshop-dynamics.abr"));
  std::string error;
  const auto result = patchy::psd::read_abr(bytes, error);
  CHECK(result.has_value());
  CHECK(error.empty());
  CHECK(result->warnings.empty());
  CHECK(result->brushes.size() == 1);

  const auto& brush = result->brushes.front();
  CHECK(brush.name == "Patchy Dynamics Probe Dyn");
  CHECK(approx(brush.spacing, 0.40));
  CHECK(approx(brush.base_angle_degrees, 30.0));
  CHECK(approx(brush.base_roundness, 60.0));
  CHECK(brush.width > 0 && brush.width <= 24);
  CHECK(brush.height > 0 && brush.height <= 24);

  const auto& dynamics = brush.dynamics;
  CHECK(dynamics.active());
  CHECK(approx(dynamics.size_jitter, 0.37));
  CHECK(approx(dynamics.minimum_diameter, 0.20));
  CHECK(approx(dynamics.angle_jitter, 0.10));
  CHECK(dynamics.angle_control == patchy::BrushDynamicControl::Direction);
  CHECK(dynamics.angle_fade_steps == 25);
  CHECK(approx(dynamics.roundness_jitter, 0.30));
  CHECK(approx(dynamics.minimum_roundness, 0.25));
  CHECK(dynamics.flip_x_jitter);
  CHECK(!dynamics.flip_y_jitter);
  CHECK(approx(dynamics.scatter, 2.50));
  CHECK(!dynamics.scatter_both_axes);
  CHECK(dynamics.count == 4);
  CHECK(approx(dynamics.count_jitter, 0.50));
  CHECK(approx(dynamics.opacity_jitter, 0.25));
  // This fixture's non-angle bVTy values are all 0 (no control chosen in Photoshop): they must
  // import as the follow-the-global-preferences default for size/roundness/opacity and plain
  // Off for scatter/count, with a zero Transfer minimum.
  CHECK(dynamics.size_control == patchy::BrushDynamicControl::GlobalDefault);
  CHECK(dynamics.roundness_control == patchy::BrushDynamicControl::GlobalDefault);
  CHECK(dynamics.opacity_control == patchy::BrushDynamicControl::GlobalDefault);
  CHECK(dynamics.scatter_control == patchy::BrushDynamicControl::Off);
  CHECK(dynamics.count_control == patchy::BrushDynamicControl::Off);
  CHECK(approx(dynamics.minimum_opacity, 0.0));

  // The dual-brush variant imports its supported settings but warns about the dual brush.
  const auto dual_bytes =
      read_binary_file(patchy::test::committed_abr_fixture_path("photoshop-dual-brush.abr"));
  const auto dual_result = patchy::psd::read_abr(dual_bytes, error);
  CHECK(dual_result.has_value());
  CHECK(dual_result->brushes.size() == 1);
  CHECK(dual_result->brushes.front().name == "Patchy Dual Probe");
  CHECK(approx(dual_result->brushes.front().dynamics.size_jitter, 0.55));
  CHECK(dual_result->warnings.size() == 1);
  CHECK(dual_result->warnings.front().find("Patchy Dual Probe") != std::string::npos);
  CHECK(dual_result->warnings.front().find("dual brush") != std::string::npos);
}

void abr_myer_brushes_have_default_dynamics() {
  // A real-world set exported with dynamics disabled: every brush must come back inactive with
  // neutral base shape, and the use*-flag booleans must not trip the unsupported-feature warning.
  const auto bytes = read_binary_file(patchy::test::committed_abr_fixture_path("myer-settlement-brushes.abr"));
  std::string error;
  const auto result = patchy::psd::read_abr(bytes, error);
  CHECK(result.has_value());
  CHECK(result->warnings.empty());
  for (const auto& brush : result->brushes) {
    CHECK(!brush.dynamics.active());
    CHECK(brush.base_roundness > 0.0 && brush.base_roundness <= 100.0);
  }
}

// --- v6 'desc' synthesis helpers, mirroring read_descriptor's TLV layout ---

void write_desc_unicode_string(patchy::psd::BigEndianWriter& writer, std::u16string_view text) {
  writer.write_u32(static_cast<std::uint32_t>(text.size()));
  for (const auto unit : text) {
    writer.write_u16(static_cast<std::uint16_t>(unit));
  }
}

void write_desc_ascii(patchy::psd::BigEndianWriter& writer, std::string_view text) {
  for (const auto ch : text) {
    writer.write_u8(static_cast<std::uint8_t>(ch));
  }
}

// Keys/class ids: 4-char codes use the length-0 signature form, longer ids are length-prefixed.
void write_desc_id(patchy::psd::BigEndianWriter& writer, std::string_view id) {
  writer.write_u32(id.size() == 4U ? 0U : static_cast<std::uint32_t>(id.size()));
  write_desc_ascii(writer, id);
}

void write_desc_header(patchy::psd::BigEndianWriter& writer, std::string_view class_id,
                       std::uint32_t item_count) {
  write_desc_unicode_string(writer, u"");
  write_desc_id(writer, class_id);
  writer.write_u32(item_count);
}

void write_desc_double(patchy::psd::BigEndianWriter& writer, std::string_view key, double value) {
  write_desc_id(writer, key);
  write_desc_ascii(writer, "doub");
  writer.write_u64(std::bit_cast<std::uint64_t>(value));
}

void write_desc_long(patchy::psd::BigEndianWriter& writer, std::string_view key, std::int32_t value) {
  write_desc_id(writer, key);
  write_desc_ascii(writer, "long");
  writer.write_u32(static_cast<std::uint32_t>(value));
}

void write_desc_bool(patchy::psd::BigEndianWriter& writer, std::string_view key, bool value) {
  write_desc_id(writer, key);
  write_desc_ascii(writer, "bool");
  writer.write_u8(value ? 1U : 0U);
}

// One 'brVr' variation object: control, fade steps, jitter %, minimum %.
void write_desc_variation(patchy::psd::BigEndianWriter& writer, std::string_view key, int bvty,
                          int fade_steps, double jitter_percent, double minimum_percent) {
  write_desc_id(writer, key);
  write_desc_ascii(writer, "Objc");
  write_desc_header(writer, "brVr", 4);
  write_desc_long(writer, "bVTy", bvty);
  write_desc_long(writer, "fStp", fade_steps);
  write_desc_double(writer, "jitter", jitter_percent);
  write_desc_double(writer, "Mnm ", minimum_percent);
}

void abr_v6_desc_controls_import() {
  // The committed Photoshop fixture only exercises bVTy 0 (no control chosen), so this
  // synthesized v6 file pins the explicit control mappings: every dynamic carries a distinct
  // bVTy, per-dynamic fade steps, the Transfer minimum, Direction degrading to Off on a
  // non-angle dynamic, and Stylus Wheel (4) importing as a real control.
  patchy::psd::BigEndianWriter desc;
  desc.write_u32(16);  // descriptor version
  write_desc_header(desc, "null", 1);
  write_desc_id(desc, "Brsh");
  write_desc_ascii(desc, "VlLs");
  desc.write_u32(1);  // one preset
  write_desc_ascii(desc, "Objc");
  write_desc_header(desc, "brushPreset", 17);
  {
    write_desc_id(desc, "Nm  ");
    write_desc_ascii(desc, "TEXT");
    write_desc_unicode_string(desc, u"Controls Probe");
    write_desc_id(desc, "Brsh");
    write_desc_ascii(desc, "Objc");
    write_desc_header(desc, "sampledBrush", 3);
    write_desc_double(desc, "Spcn", 25.0);
    write_desc_double(desc, "Angl", 0.0);
    write_desc_double(desc, "Rndn", 100.0);
    write_desc_bool(desc, "useTipDynamics", true);
    write_desc_variation(desc, "szVr", 2, 12, 40.0, 30.0);            // Pen Pressure
    write_desc_double(desc, "minimumDiameter", 30.0);
    write_desc_variation(desc, "angleDynamics", 4, 25, 10.0, 0.0);    // Stylus Wheel
    write_desc_variation(desc, "roundnessDynamics", 1, 40, 20.0, 0.0);  // Fade, 40 steps
    write_desc_double(desc, "minimumRoundness", 25.0);
    write_desc_bool(desc, "flipX", false);
    write_desc_bool(desc, "flipY", false);
    write_desc_bool(desc, "useScatter", true);
    write_desc_variation(desc, "scatterDynamics", 3, 33, 150.0, 0.0);  // Pen Tilt
    write_desc_bool(desc, "bothAxes", true);
    write_desc_double(desc, "Cnt ", 3.0);
    write_desc_variation(desc, "countDynamics", 7, 25, 50.0, 0.0);  // Direction -> Off (angle-only)
    write_desc_bool(desc, "usePaintDynamics", true);
    write_desc_variation(desc, "opVr", 5, 60, 25.0, 30.0);  // Rotation; Mnm -> minimum opacity
  }

  // 'samp' block: one subversion-1 entry (47-byte key skip), 4x4 raw 8-bit mask.
  patchy::psd::BigEndianWriter samp;
  {
    patchy::psd::BigEndianWriter entry;
    for (int i = 0; i < 47; ++i) {
      entry.write_u8(0);  // UUID string + fixed-layout fields the reader skips
    }
    entry.write_u32(0);  // top
    entry.write_u32(0);  // left
    entry.write_u32(4);  // bottom
    entry.write_u32(4);  // right
    entry.write_u16(8);  // depth
    entry.write_u8(0);   // raw rows
    for (int i = 0; i < 16; ++i) {
      entry.write_u8(255);
    }
    const auto body = entry.bytes();
    samp.write_u32(static_cast<std::uint32_t>(body.size()));
    samp.write_bytes(body);
    while (samp.bytes().size() % 4U != 0U) {
      samp.write_u8(0);
    }
  }

  patchy::psd::BigEndianWriter file;
  file.write_u16(6);  // version
  file.write_u16(1);  // subversion
  const auto write_block = [&file](std::string_view key, const std::vector<std::uint8_t>& block) {
    write_desc_ascii(file, "8BIM");
    write_desc_ascii(file, key);
    file.write_u32(static_cast<std::uint32_t>(block.size()));
    file.write_bytes(block);
    while (file.bytes().size() % 4U != 0U) {
      file.write_u8(0);
    }
  };
  write_block("samp", samp.bytes());
  write_block("desc", desc.bytes());

  std::string error;
  const auto result = patchy::psd::read_abr(file.bytes(), error);
  CHECK(result.has_value());
  CHECK(error.empty());
  CHECK(result->warnings.empty());
  CHECK(result->brushes.size() == 1);

  const auto approx = [](double value, double expected) { return std::abs(value - expected) < 1e-9; };
  const auto& brush = result->brushes.front();
  CHECK(brush.name == "Controls Probe");
  const auto& dynamics = brush.dynamics;
  CHECK(dynamics.size_control == patchy::BrushDynamicControl::PenPressure);
  CHECK(dynamics.size_fade_steps == 12);
  CHECK(approx(dynamics.size_jitter, 0.40));
  CHECK(approx(dynamics.minimum_diameter, 0.30));
  CHECK(dynamics.angle_control == patchy::BrushDynamicControl::StylusWheel);
  CHECK(dynamics.roundness_control == patchy::BrushDynamicControl::Fade);
  CHECK(dynamics.roundness_fade_steps == 40);
  CHECK(dynamics.scatter_control == patchy::BrushDynamicControl::PenTilt);
  CHECK(dynamics.scatter_fade_steps == 33);
  CHECK(approx(dynamics.scatter, 1.50));
  CHECK(dynamics.scatter_both_axes);
  CHECK(dynamics.count == 3);
  CHECK(dynamics.count_control == patchy::BrushDynamicControl::Off);  // Direction is angle-only
  CHECK(approx(dynamics.count_jitter, 0.50));
  CHECK(dynamics.opacity_control == patchy::BrushDynamicControl::PenRotation);
  CHECK(dynamics.opacity_fade_steps == 60);
  CHECK(approx(dynamics.opacity_jitter, 0.25));
  CHECK(approx(dynamics.minimum_opacity, 0.30));
}

// Builds a legacy v1/v2 sampled-brush entry body (without the type/size prefix).
std::vector<std::uint8_t> make_abr_v12_sampled_body(std::uint16_t version, std::u16string_view name,
                                                    std::uint16_t spacing, std::int32_t width,
                                                    std::int32_t height, std::uint16_t depth,
                                                    std::uint8_t compression,
                                                    std::span<const std::uint8_t> data) {
  patchy::psd::BigEndianWriter writer;
  writer.write_u32(0);        // misc
  writer.write_u16(spacing);  // percent
  if (version == 2U) {
    writer.write_u32(static_cast<std::uint32_t>(name.size()));
    for (const auto unit : name) {
      writer.write_u16(static_cast<std::uint16_t>(unit));
    }
  }
  writer.write_u8(1);  // antialiasing
  for (int i = 0; i < 4; ++i) {
    writer.write_u16(0);  // legacy short bounds (unused)
  }
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(height));
  writer.write_u32(static_cast<std::uint32_t>(width));
  writer.write_u16(depth);
  writer.write_u8(compression);
  writer.write_bytes(data);
  return writer.bytes();
}

std::vector<std::uint8_t> make_abr_v12_file(std::uint16_t version,
                                            std::span<const std::pair<std::uint16_t, std::vector<std::uint8_t>>>
                                                entries) {
  patchy::psd::BigEndianWriter writer;
  writer.write_u16(version);
  writer.write_u16(static_cast<std::uint16_t>(entries.size()));
  for (const auto& [type, body] : entries) {
    writer.write_u16(type);
    writer.write_u32(static_cast<std::uint32_t>(body.size()));
    writer.write_bytes(body);
  }
  return writer.bytes();
}

void abr_v1_parses_sampled_brush_and_skips_computed() {
  // 4x4 raw mask with content only in the middle 2x2 — the reader crops to content.
  std::vector<std::uint8_t> mask(16, 0);
  mask[1U * 4U + 1U] = 255;
  mask[1U * 4U + 2U] = 255;
  mask[2U * 4U + 1U] = 255;
  mask[2U * 4U + 2U] = 200;

  const std::vector<std::pair<std::uint16_t, std::vector<std::uint8_t>>> entries = {
      {1U, std::vector<std::uint8_t>(14, 0)},  // computed brush: no bitmap, skipped with warning
      {2U, make_abr_v12_sampled_body(1, {}, 25, 4, 4, 8, 0, mask)},
  };
  const auto bytes = make_abr_v12_file(1, entries);

  std::string error;
  const auto result = patchy::psd::read_abr(bytes, error);
  CHECK(result.has_value());
  CHECK(result->warnings.size() == 1);
  CHECK(result->brushes.size() == 1);
  const auto& brush = result->brushes.front();
  CHECK(brush.width == 2);
  CHECK(brush.height == 2);
  CHECK(brush.spacing == 0.25);
  CHECK(brush.mask == (std::vector<std::uint8_t>{255, 255, 255, 200}));
}

void abr_v2_parses_named_rle_and_16bit_brushes() {
  // 8x2 all-opaque mask, RLE-compressed: per-row byte counts then PackBits rows.
  patchy::psd::BigEndianWriter rle;
  rle.write_u16(2);  // row 0 compressed length
  rle.write_u16(2);  // row 1 compressed length
  for (int row = 0; row < 2; ++row) {
    rle.write_u8(0xF9);  // repeat next byte 8 times
    rle.write_u8(0xFF);
  }

  // 2x1 16-bit raw mask: big-endian 0xFF00, 0x8000 → 8-bit 255, 128.
  patchy::psd::BigEndianWriter deep;
  deep.write_u16(0xFF00);
  deep.write_u16(0x8000);

  const std::vector<std::pair<std::uint16_t, std::vector<std::uint8_t>>> entries = {
      {2U, make_abr_v12_sampled_body(2, u"Dots", 50, 8, 2, 8, 1, rle.bytes())},
      {2U, make_abr_v12_sampled_body(2, u"Deep", 25, 2, 1, 16, 0, deep.bytes())},
  };
  const auto bytes = make_abr_v12_file(2, entries);

  std::string error;
  const auto result = patchy::psd::read_abr(bytes, error);
  CHECK(result.has_value());
  CHECK(result->warnings.empty());
  CHECK(result->brushes.size() == 2);
  CHECK(result->brushes[0].name == "Dots");
  CHECK(result->brushes[0].width == 8);
  CHECK(result->brushes[0].height == 2);
  CHECK(result->brushes[0].spacing == 0.5);
  CHECK(std::all_of(result->brushes[0].mask.begin(), result->brushes[0].mask.end(),
                    [](std::uint8_t value) { return value == 255U; }));
  CHECK(result->brushes[1].name == "Deep");
  CHECK(result->brushes[1].mask == (std::vector<std::uint8_t>{255, 128}));
}

void abr_rejects_corrupt_truncated_and_empty_files() {
  std::string error;

  // Empty file.
  CHECK(!patchy::psd::read_abr({}, error).has_value());
  CHECK(!error.empty());

  // Unsupported version.
  patchy::psd::BigEndianWriter bad_version;
  bad_version.write_u16(3);
  CHECK(!patchy::psd::read_abr(bad_version.bytes(), error).has_value());
  CHECK(error.find("Unsupported ABR version") != std::string::npos);

  // Computed-only file: parses but contains nothing usable.
  const std::vector<std::pair<std::uint16_t, std::vector<std::uint8_t>>> computed_only = {
      {1U, std::vector<std::uint8_t>(14, 0)},
  };
  CHECK(!patchy::psd::read_abr(make_abr_v12_file(1, computed_only), error).has_value());
  CHECK(error.find("no sampled") != std::string::npos);

  // Entry size larger than the file.
  patchy::psd::BigEndianWriter truncated;
  truncated.write_u16(1);
  truncated.write_u16(1);
  truncated.write_u16(2);
  truncated.write_u32(1000);
  CHECK(!patchy::psd::read_abr(truncated.bytes(), error).has_value());
  CHECK(!error.empty());

  // The v6 fixture truncated at every structural boundary must error, never crash.
  const auto fixture =
      read_binary_file(patchy::test::committed_abr_fixture_path("myer-settlement-brushes.abr"));
  for (const std::size_t length : {std::size_t{1}, std::size_t{3}, std::size_t{8}, std::size_t{11},
                                   std::size_t{50}, std::size_t{347}}) {
    const auto prefix = std::span<const std::uint8_t>(fixture.data(), std::min(length, fixture.size()));
    const auto result = patchy::psd::read_abr(prefix, error);
    if (result.has_value()) {
      // A prefix that happens to end exactly between tagged blocks can parse; it must then
      // still deliver structurally valid brushes.
      for (const auto& brush : result->brushes) {
        CHECK(brush.mask.size() ==
              static_cast<std::size_t>(brush.width) * static_cast<std::size_t>(brush.height));
      }
    } else {
      CHECK(!error.empty());
    }
  }
}

void tool_one_pixel_brush_segment_snaps_fractional_points_to_one_pixel() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);

  auto options = tool_options(0, 0, 0);
  options.brush_size = 1;
  options.brush_softness = 100;
  const auto dirty = patchy::paint_brush_segment(document, layer_id, 20.1, 20.4, 20.8, 20.4, options, false);

  CHECK(dirty.x == 20);
  CHECK(dirty.y == 20);
  CHECK(dirty.width == 1);
  CHECK(dirty.height == 1);
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  CHECK(layer->pixels().pixel(20, 20)[3] == 255);
  CHECK(layer->pixels().pixel(21, 20)[3] == 0);
}

void tool_wide_brush_segment_is_fast_and_writes_artifact() {
  patchy::Document document(1600, 1000, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(1600, 1000, 255, 255, 255));
  patchy::PixelBuffer pixels(1600, 1000, patchy::PixelFormat::rgba8());
  pixels.clear(0);
  const auto layer_id = document.add_pixel_layer("Paint", std::move(pixels)).id();

  auto options = tool_options(20, 80, 230);
  options.brush_size = 240;
  const auto started = std::chrono::steady_clock::now();
  const auto dirty = patchy::paint_brush_segment(document, layer_id, 140, 500, 1460, 500, options, false);
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started)
                              .count();

  CHECK(!dirty.empty());
  CHECK(elapsed_ms < 1000);
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  CHECK(layer->bounds().contains(800, 500));
  const auto* center = layer->pixels().pixel(800 - layer->bounds().x, 500 - layer->bounds().y);
  CHECK(center[0] == 20);
  CHECK(center[1] == 80);
  CHECK(center[2] == 230);
  CHECK(center[3] == 255);
  write_bmp_artifact("tool_wide_brush_segment", document);
}

void tool_brush_segment_accepts_float_endpoints() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(20, 120, 240);
  options.brush_size = 9;
  options.brush_softness = 100;

  const auto dirty = patchy::paint_brush_segment(document, layer_id, 10.25, 20.5, 45.75, 21.25, options, false);

  CHECK(!dirty.empty());
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  CHECK(layer->bounds().contains(28, 21));
  const auto* center = layer->pixels().pixel(28 - layer->bounds().x, 21 - layer->bounds().y);
  CHECK(center[0] == 20);
  CHECK(center[1] == 120);
  CHECK(center[2] == 240);
  CHECK(center[3] > 0);
}

void tool_brush_roundness_and_angle_shape_dabs() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(20, 120, 240);
  options.brush_size = 31;
  options.brush_softness = 0;
  options.brush_roundness = 25;
  options.brush_angle_degrees = 0.0;

  const auto dirty = patchy::paint_brush(document, layer_id, 32, 24, options, false);
  CHECK(!dirty.empty());
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  const auto& pixels = layer->pixels();
  int horizontal = 0;
  for (std::int32_t x = 0; x < pixels.width(); ++x) {
    if (pixels.pixel(x, 24)[3] > 0U) {
      ++horizontal;
    }
  }
  int vertical = 0;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    if (pixels.pixel(32, y)[3] > 0U) {
      ++vertical;
    }
  }
  CHECK(horizontal > vertical * 2);

  auto rotated = make_tool_document();
  const auto rotated_layer_id = active_tool_layer(rotated);
  options.brush_angle_degrees = 90.0;
  CHECK(!patchy::paint_brush(rotated, rotated_layer_id, 32, 24, options, false).empty());
  const auto& rotated_pixels = rotated.find_layer(rotated_layer_id)->pixels();
  horizontal = 0;
  for (std::int32_t x = 0; x < rotated_pixels.width(); ++x) {
    if (rotated_pixels.pixel(x, 24)[3] > 0U) {
      ++horizontal;
    }
  }
  vertical = 0;
  for (std::int32_t y = 0; y < rotated_pixels.height(); ++y) {
    if (rotated_pixels.pixel(32, y)[3] > 0U) {
      ++vertical;
    }
  }
  CHECK(vertical > horizontal * 2);
}

void tool_eraser_clears_alpha_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options();
  CHECK(!patchy::paint_brush(document, layer_id, 20, 20, options, false).empty());
  CHECK(!patchy::paint_brush(document, layer_id, 20, 20, options, true).empty());
  const auto* px = document.find_layer(layer_id)->pixels().pixel(20, 20);
  CHECK(px[3] == 0);
  write_bmp_artifact("tool_eraser", document);
}

void tool_eraser_converts_rgb_layer_to_transparency() {
  patchy::Document document(12, 12, patchy::PixelFormat::rgb8());
  const auto layer_id = document.add_pixel_layer("Background", solid_rgb(12, 12, 255, 255, 255)).id();
  auto options = tool_options();
  options.brush_size = 5;
  CHECK(!patchy::paint_brush(document, layer_id, 6, 6, options, true).empty());
  const auto& pixels = document.find_layer(layer_id)->pixels();
  CHECK(pixels.format() == patchy::PixelFormat::rgba8());
  CHECK(pixels.pixel(6, 6)[3] == 0);
  CHECK(pixels.pixel(0, 0)[3] == 255);
}

void tool_smudge_drags_source_pixels_and_writes_artifact() {
  patchy::Document document(80, 40, patchy::PixelFormat::rgb8());
  auto pixels = solid_rgba(80, 40, 255, 255, 255, 255);
  for (std::int32_t y = 8; y < 32; ++y) {
    for (std::int32_t x = 8; x < 24; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 220;
      px[1] = 30;
      px[2] = 20;
      px[3] = 255;
    }
  }
  const auto layer_id = document.add_pixel_layer("Smudge", std::move(pixels)).id();
  auto options = tool_options();
  options.brush_size = 13;

  const auto dirty = patchy::smudge_brush_segment(document, layer_id, 20, 20, 48, 20, options);
  CHECK(!dirty.empty());
  const auto* smeared = document.find_layer(layer_id)->pixels().pixel(48, 20);
  const auto* untouched = document.find_layer(layer_id)->pixels().pixel(70, 20);
  CHECK(smeared[0] == 220);
  CHECK(smeared[1] == 30);
  CHECK(smeared[2] == 20);
  CHECK(untouched[0] == 255);
  CHECK(untouched[1] == 255);
  CHECK(untouched[2] == 255);
  write_bmp_artifact("tool_smudge", document);
}

void tool_line_draws_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  const auto dirty = patchy::draw_line(document, layer_id, 5, 5, 55, 40, tool_options(20, 180, 80), false);
  CHECK(!dirty.empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(30, 22)[3] > 0);
  write_bmp_artifact("tool_line", document);
}

void tool_rectangle_draws_outline_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(255, 120, 0);
  options.brush_size = 3;
  const auto dirty = patchy::draw_rectangle(document, layer_id, patchy::Rect{10, 8, 28, 20}, options, false);
  CHECK(!dirty.empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(10, 8)[3] > 0);
  write_bmp_artifact("tool_rectangle", document);
}

void tool_ellipse_draws_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(150, 40, 220);
  options.brush_size = 3;
  const auto dirty = patchy::draw_ellipse(document, layer_id, patchy::Rect{12, 10, 30, 22}, options, false);
  CHECK(!dirty.empty());
  write_bmp_artifact("tool_ellipse", document);
}

void tool_filled_ellipse_uses_direct_fill_and_writes_artifact() {
  patchy::Document document(1200, 900, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(1200, 900, patchy::PixelFormat::rgba8());
  pixels.clear(0);
  const auto layer_id = document.add_pixel_layer("Filled Ellipse", std::move(pixels)).id();

  auto options = tool_options(20, 150, 240);
  options.fill_shapes = true;
  options.brush_size = 96;

  const auto started = std::chrono::steady_clock::now();
  const auto dirty = patchy::draw_ellipse(document, layer_id, patchy::Rect{120, 90, 900, 620}, options, false);
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started)
                              .count();

  CHECK(!dirty.empty());
  CHECK(elapsed_ms < 1000);
  const auto& filled = document.find_layer(layer_id)->pixels();
  CHECK(filled.pixel(570, 400)[0] == 20);
  CHECK(filled.pixel(570, 400)[1] == 150);
  CHECK(filled.pixel(570, 400)[2] == 240);
  CHECK(filled.pixel(570, 400)[3] == 255);
  CHECK(filled.pixel(120, 90)[3] == 0);
  write_bmp_artifact("tool_filled_ellipse", document);
}

void tool_thick_ellipse_outline_avoids_buildup() {
  patchy::Document document(200, 200, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(200, 200, patchy::PixelFormat::rgba8());
  pixels.clear(0);
  const auto layer_id = document.add_pixel_layer("Outline", std::move(pixels)).id();

  auto options = tool_options(255, 0, 0);
  options.primary.a = 128;  // 50% opacity
  options.brush_size = 24;  // thick ring
  options.fill_shapes = false;
  const auto dirty = patchy::draw_ellipse(document, layer_id, patchy::Rect{40, 40, 120, 120}, options, false);
  CHECK(!dirty.empty());

  // Each pixel is composited exactly once, so the ring alpha stays near the single-stamp value
  // (~128). Under the old 720-segment brush-stamping the heavy overlaps built up toward 255.
  const auto& out = document.find_layer(layer_id)->pixels();
  std::uint8_t max_alpha = 0;
  for (int y = 0; y < 200; ++y) {
    for (int x = 0; x < 200; ++x) {
      max_alpha = std::max(max_alpha, out.pixel(x, y)[3]);
    }
  }
  CHECK(max_alpha > 0);
  CHECK(max_alpha <= 140);
  write_bmp_artifact("tool_thick_ellipse_outline", document);
}

void tool_filled_ellipse_respects_softness() {
  const auto draw = [](int softness) {
    patchy::Document document(200, 200, patchy::PixelFormat::rgb8());
    patchy::PixelBuffer pixels(200, 200, patchy::PixelFormat::rgba8());
    pixels.clear(0);
    const auto layer_id = document.add_pixel_layer("SoftFill", std::move(pixels)).id();
    auto options = tool_options(10, 20, 30);
    options.fill_shapes = true;
    options.brush_size = 60;
    options.brush_softness = softness;
    CHECK(!patchy::draw_ellipse(document, layer_id, patchy::Rect{40, 40, 120, 120}, options, false).empty());
    return document.find_layer(layer_id)->pixels().pixel(157, 100)[3];
  };

  const auto hard_edge = draw(0);
  const auto soft_edge = draw(90);
  // Soft=0 keeps a crisp (essentially binary) edge; Soft>0 feathers it so the same near-edge pixel
  // ends up only partially covered — the old fill ignored softness entirely.
  CHECK(hard_edge == 255);
  CHECK(soft_edge > 0);
  CHECK(soft_edge < hard_edge);
}

void tool_ellipse_outline_thickness_is_uniform() {
  // A thick outline on an elongated ellipse must keep uniform ring thickness — the exact
  // closest-point distance gives matching coverage at the major-axis tip and minor-axis tip for the
  // same outward offset, where the cheap first-order estimate would not.
  auto options = tool_options(0, 0, 0);
  options.brush_size = 12;     // half-thickness 6
  options.brush_softness = 50; // band 3
  options.fill_shapes = false;
  const auto params =
      patchy::make_shape_coverage_params(patchy::Rect{30, 30, 200, 40}, options, patchy::ShapeKind::Ellipse);
  // center (130,50), rx 100, ry 20. Major tip at x=230; minor tip at y=70.
  const auto major = patchy::shape_pixel_coverage(params, 236, 50);
  const auto minor = patchy::shape_pixel_coverage(params, 130, 76);
  CHECK(major > 0.02F);
  CHECK(major < 0.95F);
  CHECK(minor > 0.02F);
  CHECK(minor < 0.95F);
  CHECK(std::abs(major - minor) < 0.15F);
}

void tool_rounded_rectangle_rounds_corners() {
  const auto draw = [](int radius) {
    patchy::Document document(140, 120, patchy::PixelFormat::rgb8());
    patchy::PixelBuffer pixels(140, 120, patchy::PixelFormat::rgba8());
    pixels.clear(0);
    const auto layer_id = document.add_pixel_layer("RoundRect", std::move(pixels)).id();
    auto options = tool_options(200, 80, 40);
    options.fill_shapes = true;
    options.shape_corner_radius = radius;
    CHECK(!patchy::draw_rectangle(document, layer_id, patchy::Rect{20, 20, 80, 60}, options, false).empty());
    return document.find_layer(layer_id)->pixels();
  };

  const auto& sharp = draw(0);
  CHECK(sharp.pixel(21, 21)[3] == 255);  // sharp corner is filled

  const auto& rounded = draw(25);
  CHECK(rounded.pixel(21, 21)[3] == 0);    // corner rounded away
  CHECK(rounded.pixel(60, 21)[3] == 255);  // top-edge midpoint still filled
  CHECK(rounded.pixel(60, 50)[3] == 255);  // interior filled
}

void tool_fill_rect_honors_opacity_and_softness_feather() {
  patchy::Document document(120, 120, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(120, 120, patchy::PixelFormat::rgba8());
  pixels.clear(0);
  const auto layer_id = document.add_pixel_layer("Fill", std::move(pixels)).id();

  auto options = tool_options(200, 50, 50);
  options.primary.a = 128;                       // 50% opacity
  options.selection = patchy::Rect{20, 20, 80, 80};
  options.fill_softness_feather = 12.0;          // inward edge feather band (px)

  CHECK(!patchy::fill_rect(document, layer_id, *options.selection, options).empty());
  const auto& filled = document.find_layer(layer_id)->pixels();
  // Deep inside: full feather coverage, alpha scaled by opacity (~128, not 255).
  const auto center_alpha = filled.pixel(60, 60)[3];
  CHECK(center_alpha > 110);
  CHECK(center_alpha < 150);
  // Just inside the selection edge: feathered down, so noticeably more transparent than the center.
  const auto edge_alpha = filled.pixel(21, 60)[3];
  CHECK(edge_alpha < center_alpha);
  // Outside the selection stays untouched.
  CHECK(filled.pixel(10, 60)[3] == 0);
}

void tool_fill_bucket_fills_region_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  const auto dirty = patchy::flood_fill(document, layer_id, 10, 10, tool_options(0, 180, 210));
  CHECK(!dirty.empty());
  const auto* px = document.find_layer(layer_id)->pixels().pixel(10, 10);
  CHECK(px[0] == 0);
  CHECK(px[1] == 180);
  CHECK(px[2] == 210);
  CHECK(px[3] == 255);
  write_bmp_artifact("tool_fill_bucket", document);
}

void tool_gradient_draws_foreground_to_background_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(255, 0, 0);
  options.secondary = patchy::EditColor{0, 0, 255, 255};
  const auto dirty = patchy::draw_linear_gradient(document, layer_id, 0, 0, 63, 0, options);
  CHECK(!dirty.empty());
  const auto* left = document.find_layer(layer_id)->pixels().pixel(0, 20);
  const auto* right = document.find_layer(layer_id)->pixels().pixel(63, 20);
  CHECK(left[0] == 255);
  CHECK(left[1] == 0);
  CHECK(left[2] == 0);
  CHECK(right[0] == 0);
  CHECK(right[1] == 0);
  CHECK(right[2] == 255);
  CHECK(right[3] == 255);
  write_bmp_artifact("tool_gradient", document);
}

void tool_gradient_supports_custom_stops_radial_reverse_and_alpha() {
  {
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = tool_options();
    patchy::GradientOptions gradient;
    gradient.stops = {
        patchy::GradientStop{0.0F, patchy::EditColor{255, 0, 0, 255}},
        patchy::GradientStop{0.5F, patchy::EditColor{0, 255, 0, 255}},
        patchy::GradientStop{1.0F, patchy::EditColor{0, 0, 255, 128}},
    };
    CHECK(!patchy::draw_gradient(document, layer_id, 0, 0, 63, 0, options, gradient).empty());
    const auto* middle = document.find_layer(layer_id)->pixels().pixel(32, 20);
    const auto* right = document.find_layer(layer_id)->pixels().pixel(63, 20);
    CHECK(middle[1] > 245);
    CHECK(middle[0] < 20);
    CHECK(middle[2] < 20);
    CHECK(right[2] == 255);
    CHECK(right[3] >= 127);
    CHECK(right[3] <= 129);
  }

  {
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = tool_options();
    patchy::GradientOptions gradient;
    gradient.reverse = true;
    gradient.stops = {
        patchy::GradientStop{0.0F, patchy::EditColor{255, 0, 0, 0}},
        patchy::GradientStop{1.0F, patchy::EditColor{0, 0, 255, 255}},
    };
    CHECK(!patchy::draw_gradient(document, layer_id, 0, 0, 63, 0, options, gradient).empty());
    const auto* left = document.find_layer(layer_id)->pixels().pixel(0, 20);
    const auto* right = document.find_layer(layer_id)->pixels().pixel(63, 20);
    CHECK(left[2] == 255);
    CHECK(left[3] == 255);
    CHECK(right[3] == 0);
  }

  {
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = tool_options();
    patchy::GradientOptions gradient;
    gradient.method = patchy::GradientMethod::Radial;
    gradient.opacity = 0.5F;
    gradient.stops = {
        patchy::GradientStop{0.0F, patchy::EditColor{255, 0, 0, 255}},
        patchy::GradientStop{1.0F, patchy::EditColor{0, 0, 255, 255}},
    };
    CHECK(!patchy::draw_gradient(document, layer_id, 32, 24, 42, 24, options, gradient).empty());
    const auto* center = document.find_layer(layer_id)->pixels().pixel(32, 24);
    const auto* edge = document.find_layer(layer_id)->pixels().pixel(42, 24);
    CHECK(center[0] == 255);
    CHECK(center[2] == 0);
    CHECK(center[3] >= 127);
    CHECK(center[3] <= 129);
    CHECK(edge[0] == 0);
    CHECK(edge[2] == 255);
    CHECK(edge[3] >= 127);
    CHECK(edge[3] <= 129);
  }
}

void tool_fill_selection_draws_only_selection_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(40, 200, 80);
  options.selection = patchy::Rect{8, 8, 16, 12};
  const auto dirty = patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, options);
  CHECK(dirty.x == 8);
  CHECK(dirty.y == 8);
  CHECK(document.find_layer(layer_id)->pixels().pixel(8, 8)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(2, 2)[3] == 0);
  write_bmp_artifact("tool_fill_selection", document);
}

void tool_clear_selection_erases_only_selection_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(40, 200, 80);
  CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, options).empty());
  options.selection = patchy::Rect{8, 8, 16, 12};
  const auto dirty = patchy::clear_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, options);
  CHECK(!dirty.empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(8, 8)[3] == 0);
  CHECK(document.find_layer(layer_id)->pixels().pixel(2, 2)[3] == 255);
  write_bmp_artifact("tool_clear_selection", document);
}

void tool_clear_transparent_selection_is_noop() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(40, 200, 80);
  options.selection = patchy::Rect{8, 8, 16, 12};

  CHECK(patchy::clear_rect_change_bounds(document, layer_id, patchy::Rect{0, 0, 64, 48}, options).empty());
  CHECK(patchy::clear_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, options).empty());
  CHECK(document.find_layer(layer_id)->pixels().format() == patchy::PixelFormat::rgba8());
  CHECK(document.find_layer(layer_id)->pixels().pixel(8, 8)[3] == 0);
}

void tool_clear_selected_opaque_pixels_reports_exact_bounds() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(40, 200, 80);
  CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{8, 8, 16, 12}, options).empty());

  options.selection = patchy::Rect{10, 9, 3, 2};
  const auto planned = patchy::clear_rect_change_bounds(document, layer_id, patchy::Rect{0, 0, 64, 48}, options);
  CHECK(planned.x == 10);
  CHECK(planned.y == 9);
  CHECK(planned.width == 3);
  CHECK(planned.height == 2);

  const auto dirty = patchy::clear_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, options);
  CHECK(dirty.x == 10);
  CHECK(dirty.y == 9);
  CHECK(dirty.width == 3);
  CHECK(dirty.height == 2);
  CHECK(document.find_layer(layer_id)->pixels().pixel(10, 9)[3] == 0);
  CHECK(document.find_layer(layer_id)->pixels().pixel(9, 9)[3] == 255);
}

void tool_fill_clear_gradient_respect_complex_selection_mask() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(250, 20, 40);
  options.selection = patchy::Rect{4, 4, 24, 24};
  options.selection_mask = [](std::int32_t x, std::int32_t y) { return (x >= 4 && x < 12) || (y >= 20 && y < 28); };

  CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, options).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(6, 6)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(18, 10)[3] == 0);
  CHECK(document.find_layer(layer_id)->pixels().pixel(18, 22)[3] == 255);

  options.selection_mask = [](std::int32_t, std::int32_t y) { return y >= 20 && y < 28; };
  CHECK(!patchy::clear_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, options).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(18, 22)[3] == 0);
  CHECK(document.find_layer(layer_id)->pixels().pixel(6, 6)[3] == 255);

  options.primary = patchy::EditColor{0, 0, 255, 255};
  options.secondary = patchy::EditColor{0, 255, 0, 255};
  options.selection = patchy::Rect{0, 0, 64, 48};
  options.selection_mask = [](std::int32_t x, std::int32_t y) { return x >= 40 && y < 20; };
  CHECK(!patchy::draw_linear_gradient(document, layer_id, 40, 0, 63, 0, options).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(42, 8)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(42, 24)[3] == 0);
  CHECK(document.find_layer(layer_id)->pixels().pixel(18, 10)[3] == 0);
  write_bmp_artifact("tool_complex_selection_mask_ops", document);
}

void tool_lock_transparent_pixels_preserves_alpha() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(200, 30, 40);
  options.lock_transparent_pixels = true;
  CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, options).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(10, 10)[3] == 0);

  options.lock_transparent_pixels = false;
  CHECK(!patchy::paint_brush(document, layer_id, 20, 20, options, false).empty());
  auto* painted = document.find_layer(layer_id)->pixels().pixel(20, 20);
  CHECK(painted[0] == 200);
  CHECK(painted[3] == 255);

  options.primary = patchy::EditColor{20, 90, 220, 255};
  options.lock_transparent_pixels = true;
  CHECK(!patchy::paint_brush(document, layer_id, 20, 20, options, false).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(20, 20)[0] == 20);
  CHECK(document.find_layer(layer_id)->pixels().pixel(20, 20)[3] == 255);
  CHECK(!patchy::paint_brush(document, layer_id, 4, 4, options, false).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(4, 4)[3] == 0);

  CHECK(patchy::clear_rect_change_bounds(document, layer_id, patchy::Rect{18, 18, 6, 6}, options).empty());
  CHECK(patchy::clear_rect(document, layer_id, patchy::Rect{18, 18, 6, 6}, options).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(20, 20)[3] == 255);
  write_bmp_artifact("tool_lock_transparency", document);
}

void tool_clear_rgb_selection_converts_only_when_pixels_change() {
  patchy::Document document(6, 5, patchy::PixelFormat::rgb8());
  const auto layer_id = document.add_pixel_layer("RGB", solid_rgb(6, 5, 12, 34, 56)).id();
  auto options = tool_options(40, 200, 80);
  options.selection = patchy::Rect{1, 1, 2, 2};

  const auto planned = patchy::clear_rect_change_bounds(document, layer_id, patchy::Rect{0, 0, 6, 5}, options);
  CHECK(planned.x == 1);
  CHECK(planned.y == 1);
  CHECK(planned.width == 2);
  CHECK(planned.height == 2);

  const auto dirty = patchy::clear_rect(document, layer_id, patchy::Rect{0, 0, 6, 5}, options);
  CHECK(dirty.x == 1);
  CHECK(dirty.y == 1);
  CHECK(dirty.width == 2);
  CHECK(dirty.height == 2);
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer->pixels().format() == patchy::PixelFormat::rgba8());
  CHECK(layer->pixels().pixel(1, 1)[3] == 0);
  CHECK(layer->pixels().pixel(0, 0)[3] == 255);
}

// Baseline for the palette-mode work: pins the exact bytes every core tool write
// path produces today, so the palette snap hook (EditOptions::palette_snap, null =
// legacy path) can prove that mode-off behavior stays bit-identical. If a deliberate
// rendering change lands, the failure output prints every current digest; re-pin the
// table from that output in the same change.
std::uint64_t layer_pixels_digest(const patchy::Document& document, patchy::LayerId layer_id) {
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  const auto& pixels = layer->pixels();
  const auto channels = pixels.format().channels;
  std::uint64_t hash = 1469598103934665603ULL;
  const auto mix = [&hash](std::uint8_t byte) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  };
  mix(static_cast<std::uint8_t>(pixels.width() & 0xFF));
  mix(static_cast<std::uint8_t>(pixels.height() & 0xFF));
  mix(static_cast<std::uint8_t>(channels & 0xFF));
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* px = pixels.pixel(x, y);
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        mix(px[channel]);
      }
    }
  }
  return hash;
}

void tool_write_paths_digest_baseline() {
  std::vector<std::pair<std::string, std::uint64_t>> digests;

  {  // Procedural hard round dab.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = tool_options(220, 20, 40);
    options.brush_size = 12;
    CHECK(!patchy::paint_brush(document, layer_id, 20, 20, options, false).empty());
    digests.emplace_back("procedural_dab_hard", layer_pixels_digest(document, layer_id));
  }
  {  // Procedural soft-edged segment.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = tool_options(30, 160, 220);
    options.brush_size = 15;
    options.brush_softness = 60;
    CHECK(!patchy::paint_brush_segment(document, layer_id, 10.0, 20.0, 44.0, 30.0, options, false).empty());
    digests.emplace_back("procedural_segment_soft", layer_pixels_digest(document, layer_id));
  }
  {  // Bitmap tip segment through the stateful spacing overload.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    const auto tip = make_bar_brush_tip();
    const auto mips = patchy::build_brush_tip_mips(tip);
    const auto scaled = patchy::make_scaled_brush_tip(mips, 9);
    auto options = tool_options(10, 200, 60);
    options.brush_size = 9;
    options.brush_tip = &scaled;
    patchy::BrushTipStrokeState state;
    CHECK(!patchy::paint_brush_segment(document, layer_id, 10.0, 20.0, 40.0, 28.0, options, false, state).empty());
    digests.emplace_back("bitmap_tip_segment", layer_pixels_digest(document, layer_id));
  }
  {  // Soft erase over solid RGBA content.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, tool_options(90, 140, 30)).empty());
    auto erase_options = tool_options();
    erase_options.brush_size = 14;
    erase_options.brush_softness = 40;
    CHECK(!patchy::paint_brush_segment(document, layer_id, 12.0, 12.0, 40.0, 30.0, erase_options, true).empty());
    digests.emplace_back("erase_soft_rgba", layer_pixels_digest(document, layer_id));
  }
  {  // Erase on a 3-channel layer blends toward the secondary color.
    auto document = make_tool_document();
    const auto background_id = document.layers().front().id();
    auto erase_options = tool_options();
    erase_options.brush_size = 12;
    erase_options.secondary = patchy::EditColor{40, 60, 200, 255};
    CHECK(!patchy::paint_brush_segment(document, background_id, 8.0, 8.0, 30.0, 24.0, erase_options, true).empty());
    digests.emplace_back("erase_rgb_background", layer_pixels_digest(document, background_id));
  }
  {  // Filled rounded rectangle through the signed-distance shape renderer.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = tool_options(255, 120, 0);
    options.fill_shapes = true;
    options.shape_corner_radius = 6;
    CHECK(!patchy::draw_rectangle(document, layer_id, patchy::Rect{8, 6, 40, 30}, options, false).empty());
    digests.emplace_back("shape_fill_rounded_rect", layer_pixels_digest(document, layer_id));
  }
  {  // Ellipse outline ring.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = tool_options(150, 40, 220);
    options.brush_size = 3;
    CHECK(!patchy::draw_ellipse(document, layer_id, patchy::Rect{12, 10, 30, 22}, options, false).empty());
    digests.emplace_back("shape_outline_ellipse", layer_pixels_digest(document, layer_id));
  }
  {  // fill_rect with an inward feather band and a selection rect.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = tool_options(20, 90, 200);
    options.selection = patchy::Rect{10, 8, 30, 24};
    options.fill_softness_feather = 4.0;
    CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{10, 8, 30, 24}, options).empty());
    digests.emplace_back("fill_rect_feathered_selection", layer_pixels_digest(document, layer_id));
  }
  {  // Flood fill on the white 3-channel background.
    auto document = make_tool_document();
    const auto background_id = document.layers().front().id();
    CHECK(!patchy::flood_fill(document, background_id, 5, 5, tool_options(0, 180, 210)).empty());
    digests.emplace_back("flood_fill_background", layer_pixels_digest(document, background_id));
  }
  {  // Linear gradient with alpha-varying custom stops.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    patchy::GradientOptions gradient;
    gradient.method = patchy::GradientMethod::Linear;
    gradient.opacity = 0.85F;
    gradient.stops = {{0.0F, patchy::EditColor{255, 0, 0, 255}},
                      {0.45F, patchy::EditColor{0, 200, 60, 128}},
                      {1.0F, patchy::EditColor{20, 40, 255, 0}}};
    CHECK(!patchy::draw_gradient(document, layer_id, 0, 0, 63, 47, tool_options(), gradient).empty());
    digests.emplace_back("gradient_linear_alpha_stops", layer_pixels_digest(document, layer_id));
  }
  {  // Radial gradient.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    patchy::GradientOptions gradient;
    gradient.method = patchy::GradientMethod::Radial;
    gradient.opacity = 1.0F;
    gradient.stops = {{0.0F, patchy::EditColor{250, 240, 40, 255}},
                      {1.0F, patchy::EditColor{40, 20, 120, 255}}};
    CHECK(!patchy::draw_gradient(document, layer_id, 32, 24, 60, 40, tool_options(), gradient).empty());
    digests.emplace_back("gradient_radial", layer_pixels_digest(document, layer_id));
  }
  {  // clear_rect limited by a selection.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, tool_options(200, 80, 40)).empty());
    auto options = tool_options();
    options.selection = patchy::Rect{6, 6, 20, 16};
    CHECK(!patchy::clear_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, options).empty());
    digests.emplace_back("clear_rect_selection", layer_pixels_digest(document, layer_id));
  }
  {  // Hard line.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = tool_options(20, 180, 80);
    options.brush_size = 3;
    CHECK(!patchy::draw_line(document, layer_id, 5, 5, 55, 40, options, false).empty());
    digests.emplace_back("line_hard", layer_pixels_digest(document, layer_id));
  }
  {  // Smudge drag from solid color into empty space.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 32, 48}, tool_options(220, 30, 20)).empty());
    auto options = tool_options();
    options.brush_size = 13;
    CHECK(!patchy::smudge_brush_segment(document, layer_id, 20, 20, 48, 20, options).empty());
    digests.emplace_back("smudge_segment", layer_pixels_digest(document, layer_id));
  }

  static constexpr std::array<std::pair<const char*, std::uint64_t>, 14> kExpected = {{
      {"procedural_dab_hard", 0x1f41304572a13fd8ULL},
      {"procedural_segment_soft", 0x7312aa1cfea0b16aULL},
      {"bitmap_tip_segment", 0xfb394573f9c5c112ULL},
      {"erase_soft_rgba", 0x2a54e34a973125f8ULL},
      {"erase_rgb_background", 0x41bb48610e4dd790ULL},
      {"shape_fill_rounded_rect", 0xb2f0360277d9a89dULL},
      {"shape_outline_ellipse", 0xa79e54e64046011dULL},
      {"fill_rect_feathered_selection", 0x94472d040684f83dULL},
      {"flood_fill_background", 0x77e535fda417f2b8ULL},
      {"gradient_linear_alpha_stops", 0x2eb6832d2d931867ULL},
      {"gradient_radial", 0x41832d6d40cf21eaULL},
      {"clear_rect_selection", 0x440d479ec3f19a9dULL},
      {"line_hard", 0x0e5f22f86e99266dULL},
      {"smudge_segment", 0xcc409264f89c224aULL},
  }};

  CHECK(digests.size() == kExpected.size());
  bool all_match = true;
  for (std::size_t i = 0; i < digests.size(); ++i) {
    if (digests[i].first != kExpected[i].first || digests[i].second != kExpected[i].second) {
      all_match = false;
    }
  }
  if (!all_match) {
    std::cout << "tool_write_paths_digest_baseline current digests (pin these on deliberate changes):\n";
    for (const auto& [name, value] : digests) {
      std::cout << "      {\"" << name << "\", 0x" << std::hex << std::setw(16) << std::setfill('0') << value
                << std::dec << "ULL},\n";
    }
  }
  CHECK(all_match);
}

// ---- Palette mode core (core/palette.hpp, core/palette_presets.hpp, formats/palette_io.hpp) ----

void palette_lut_snaps_within_quantization_bound_and_is_idempotent() {
  const auto* pico8 = patchy::find_builtin_palette_preset("pico8");
  CHECK(pico8 != nullptr);
  // The adversarial palette places two entries inside one 5-5-5 LUT bucket.
  const std::vector<patchy::RgbColor> adversarial = {{10, 10, 10}, {12, 10, 10}, {200, 200, 200}};

  const auto check_palette = [](std::span<const patchy::RgbColor> colors) {
    patchy::PaletteLut lut;
    lut.build(colors);
    CHECK(!lut.empty());
    CHECK(lut.colors().size() == colors.size());

    for (const auto& color : colors) {
      CHECK(lut.contains(color));
      const auto snapped = lut.snap(color.red, color.green, color.blue);
      CHECK(snapped.red == color.red);
      CHECK(snapped.green == color.green);
      CHECK(snapped.blue == color.blue);
    }

    const auto distance = [](patchy::RgbColor a, patchy::RgbColor b) {
      const auto dr = static_cast<double>(a.red) - b.red;
      const auto dg = static_cast<double>(a.green) - b.green;
      const auto db = static_cast<double>(a.blue) - b.blue;
      return std::sqrt(dr * dr + dg * dg + db * db);
    };
    for (int r = 0; r < 256; r += 15) {
      for (int g = 0; g < 256; g += 15) {
        for (int b = 0; b < 256; b += 15) {
          const auto color = patchy::RgbColor{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                                              static_cast<std::uint8_t>(b)};
          const auto snapped = lut.snap(color.red, color.green, color.blue);
          bool in_palette = false;
          for (const auto& entry : colors) {
            in_palette = in_palette || (entry.red == snapped.red && entry.green == snapped.green &&
                                        entry.blue == snapped.blue);
          }
          CHECK(in_palette);
          const auto best =
              colors[patchy::nearest_palette_index(color, colors)];
          // The 15-bit table may pick a neighbor of the true nearest entry, but
          // never by more than the bucket-center bound (2 * sqrt(3 * 4^2)).
          CHECK(distance(color, snapped) <= distance(color, best) + 13.87);
        }
      }
    }
  };

  check_palette(pico8->colors);
  check_palette(adversarial);

  patchy::PaletteLut empty;
  CHECK(empty.empty());
  const auto passthrough = empty.snap(12, 34, 56);
  CHECK(passthrough.red == 12);
  CHECK(passthrough.green == 34);
  CHECK(passthrough.blue == 56);
}

void palette_snap_pixel_thresholds_alpha_and_ignores_low_channel_buffers() {
  patchy::PaletteLut lut;
  const std::vector<patchy::RgbColor> colors = {{0, 0, 0}, {255, 255, 255}};
  lut.build(colors);
  patchy::PaletteSnapContext context;
  context.lut = &lut;

  std::array<std::uint8_t, 4> rgba{200, 190, 180, 130};
  patchy::snap_pixel_to_palette(rgba.data(), 4, context);
  CHECK(rgba[0] == 255);
  CHECK(rgba[1] == 255);
  CHECK(rgba[2] == 255);
  CHECK(rgba[3] == 255);

  rgba = {30, 20, 10, 127};
  patchy::snap_pixel_to_palette(rgba.data(), 4, context);
  CHECK(rgba[0] == 0);
  CHECK(rgba[3] == 0);

  std::array<std::uint8_t, 3> rgb{200, 190, 180};
  patchy::snap_pixel_to_palette(rgb.data(), 3, context);
  CHECK(rgb[0] == 255);

  std::array<std::uint8_t, 1> gray{99};
  patchy::snap_pixel_to_palette(gray.data(), 1, context);
  CHECK(gray[0] == 99);

  std::array<std::uint8_t, 4> untouched{1, 2, 3, 4};
  patchy::snap_pixel_to_palette(untouched.data(), 4, patchy::PaletteSnapContext{});
  CHECK(untouched[0] == 1);
  CHECK(untouched[3] == 4);
}

void palette_remap_exact_color_is_lossless_and_bounded() {
  patchy::PixelBuffer pixels(8, 6, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 6; ++y) {
    for (std::int32_t x = 0; x < 8; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 10;
      px[1] = 20;
      px[2] = 30;
      px[3] = static_cast<std::uint8_t>(40 + x);
    }
  }
  // A near-miss color one channel off must not be remapped.
  pixels.pixel(0, 0)[0] = 11;
  pixels.pixel(3, 2)[0] = 10;
  pixels.pixel(7, 5)[2] = 31;

  const auto dirty = patchy::remap_exact_color(pixels, patchy::RgbColor{10, 20, 30}, patchy::RgbColor{200, 100, 50});
  CHECK(!dirty.empty());
  CHECK(dirty.x == 0);
  CHECK(dirty.y == 0);
  CHECK(dirty.width == 8);
  CHECK(dirty.height == 6);
  CHECK(pixels.pixel(0, 0)[0] == 11);   // near miss untouched
  CHECK(pixels.pixel(7, 5)[2] == 31);   // near miss untouched
  CHECK(pixels.pixel(3, 2)[0] == 200);
  CHECK(pixels.pixel(3, 2)[1] == 100);
  CHECK(pixels.pixel(3, 2)[2] == 50);
  CHECK(pixels.pixel(3, 2)[3] == 43);   // alpha preserved

  CHECK(patchy::remap_exact_color(pixels, patchy::RgbColor{5, 5, 5}, patchy::RgbColor{6, 6, 6}).empty());
  CHECK(patchy::remap_exact_color(pixels, patchy::RgbColor{200, 100, 50}, patchy::RgbColor{200, 100, 50}).empty());
}

void palette_quantize_uses_exact_colors_then_median_cut() {
  patchy::PixelBuffer few(4, 2, patchy::PixelFormat::rgba8());
  const std::array<patchy::RgbColor, 4> wanted = {{{5, 5, 5}, {250, 0, 0}, {0, 250, 0}, {0, 0, 250}}};
  for (std::int32_t x = 0; x < 4; ++x) {
    for (std::int32_t y = 0; y < 2; ++y) {
      auto* px = few.pixel(x, y);
      px[0] = wanted[static_cast<std::size_t>(x)].red;
      px[1] = wanted[static_cast<std::size_t>(x)].green;
      px[2] = wanted[static_cast<std::size_t>(x)].blue;
      px[3] = 255;
    }
  }
  // Transparent pixels must not contribute colors.
  few.pixel(3, 1)[0] = 99;
  few.pixel(3, 1)[3] = 0;

  const auto exact = patchy::exact_palette_from_pixels(few, 256);
  CHECK(exact.has_value());
  CHECK(exact->colors.size() == 4);
  const auto quantized_few = patchy::quantize_to_palette(few, 16);
  CHECK(quantized_few.colors.size() == 4);

  patchy::PixelBuffer many(64, 1, patchy::PixelFormat::rgb8());
  for (std::int32_t x = 0; x < 64; ++x) {
    auto* px = many.pixel(x, 0);
    px[0] = static_cast<std::uint8_t>(x * 4);
    px[1] = static_cast<std::uint8_t>(255 - x * 2);
    px[2] = static_cast<std::uint8_t>(128 + x);
  }
  CHECK(!patchy::exact_palette_from_pixels(many, 16).has_value());
  const auto quantized = patchy::quantize_to_palette(many, 8);
  CHECK(quantized.colors.size() == 8);
  const auto again = patchy::quantize_to_palette(many, 8);
  CHECK(quantized.colors.size() == again.colors.size());
  for (std::size_t i = 0; i < quantized.colors.size(); ++i) {
    CHECK(patchy::palette_color_key(quantized.colors[i]) == patchy::palette_color_key(again.colors[i]));
  }
}

void palette_apply_dither_outputs_only_palette_colors_and_is_deterministic() {
  const auto* gameboy = patchy::find_builtin_palette_preset("gameboy");
  CHECK(gameboy != nullptr);
  patchy::PaletteLut lut;
  lut.build(gameboy->colors);

  const auto make_gradient = [] {
    patchy::PixelBuffer pixels(32, 32, patchy::PixelFormat::rgba8());
    for (std::int32_t y = 0; y < 32; ++y) {
      for (std::int32_t x = 0; x < 32; ++x) {
        auto* px = pixels.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(x * 8);
        px[1] = static_cast<std::uint8_t>(y * 8);
        px[2] = static_cast<std::uint8_t>((x + y) * 4);
        px[3] = static_cast<std::uint8_t>(y < 4 ? 60 : (y < 8 ? 180 : 255));
      }
    }
    return pixels;
  };

  const auto dithers = {patchy::PaletteDither::None, patchy::PaletteDither::FloydSteinberg,
                        patchy::PaletteDither::OrderedBayer4x4, patchy::PaletteDither::OrderedBayer8x8};
  patchy::PixelBuffer none_result(1, 1, patchy::PixelFormat::rgba8());
  for (const auto dither : dithers) {
    auto first = make_gradient();
    auto second = make_gradient();
    CHECK(!patchy::apply_palette_to_pixels(first, lut, dither, 128).empty());
    CHECK(!patchy::apply_palette_to_pixels(second, lut, dither, 128).empty());
    CHECK(patchy::pixels_are_palette_clean(first, lut));
    for (std::int32_t y = 0; y < 32; ++y) {
      for (std::int32_t x = 0; x < 32; ++x) {
        const auto* a = first.pixel(x, y);
        const auto* b = second.pixel(x, y);
        for (int channel = 0; channel < 4; ++channel) {
          CHECK(a[channel] == b[channel]);
        }
        CHECK(a[3] == (y < 4 ? 0 : 255));
      }
    }
    if (dither == patchy::PaletteDither::None) {
      none_result = std::move(first);
    } else if (dither == patchy::PaletteDither::FloydSteinberg) {
      // Dithering must actually change the picture relative to plain snapping.
      bool differs = false;
      for (std::int32_t y = 8; y < 32 && !differs; ++y) {
        for (std::int32_t x = 0; x < 32 && !differs; ++x) {
          const auto* a = first.pixel(x, y);
          const auto* b = none_result.pixel(x, y);
          differs = a[0] != b[0] || a[1] != b[1] || a[2] != b[2];
        }
      }
      CHECK(differs);
    }
  }

  // A palette-clean buffer stays bit-identical through a no-dither re-apply.
  auto clean = make_gradient();
  CHECK(!patchy::apply_palette_to_pixels(clean, lut, patchy::PaletteDither::None, 128).empty());
  auto reapplied = clean;
  CHECK(patchy::apply_palette_to_pixels(reapplied, lut, patchy::PaletteDither::None, 128).empty());
}

void palette_io_round_trips_all_writer_formats() {
  const auto* pico8 = patchy::find_builtin_palette_preset("pico8");
  CHECK(pico8 != nullptr);
  const std::vector<patchy::RgbColor> colors(pico8->colors.begin(), pico8->colors.end());

  const auto formats = {patchy::palette_io::PaletteFileFormat::JascPal, patchy::palette_io::PaletteFileFormat::Gpl,
                        patchy::palette_io::PaletteFileFormat::Hex, patchy::palette_io::PaletteFileFormat::Act,
                        patchy::palette_io::PaletteFileFormat::Aco};
  for (const auto format : formats) {
    const auto bytes = patchy::palette_io::write_palette_bytes(colors, format, "Test Palette");
    const auto parsed = patchy::palette_io::read_palette_bytes(bytes);
    CHECK(parsed.colors.size() == colors.size());
    for (std::size_t i = 0; i < colors.size(); ++i) {
      CHECK(patchy::palette_color_key(parsed.colors[i]) == patchy::palette_color_key(colors[i]));
    }
    if (format == patchy::palette_io::PaletteFileFormat::Gpl) {
      CHECK(parsed.name == "Test Palette");
    }
  }

  // Lospec-style hex with prefixes, blank lines, CRLF, and 8-digit entries.
  const std::string hex_text = "#ff004d\r\n\r\n00e43680\r\n1d2b53\r\n";
  const auto hex = patchy::palette_io::read_palette_bytes(
      std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(hex_text.data()), hex_text.size()));
  CHECK(hex.colors.size() == 3);
  CHECK(hex.colors[0].red == 0xFF);
  CHECK(hex.colors[0].blue == 0x4D);
  CHECK(hex.colors[1].green == 0xE4);

  // A hand-built single-color .ase file (one RGB block, name "A").
  const std::array<std::uint8_t, 12 + 6 + 24> ase = {
      'A', 'S', 'E', 'F', 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,  // header + block count 1
      0x00, 0x01, 0x00, 0x00, 0x00, 24,                                    // color block, length 24
      0x00, 0x02, 0x00, 0x41, 0x00, 0x00,                                  // name "A" + terminator
      'R', 'G', 'B', ' ',                                                  //
      0x3F, 0x80, 0x00, 0x00,                                              // 1.0f
      0x00, 0x00, 0x00, 0x00,                                              // 0.0f
      0x3F, 0x00, 0x00, 0x00,                                              // 0.5f
      0x00, 0x02,                                                          // global color type
  };
  const auto ase_parsed = patchy::palette_io::read_palette_bytes(ase);
  CHECK(ase_parsed.colors.size() == 1);
  CHECK(ase_parsed.colors[0].red == 255);
  CHECK(ase_parsed.colors[0].green == 0);
  CHECK(ase_parsed.colors[0].blue == 128);

  // .act with a 772-byte trailer carrying count + transparent index.
  auto act = patchy::palette_io::write_palette_bytes(colors, patchy::palette_io::PaletteFileFormat::Act, {});
  CHECK(act.size() == 772);
  act[770] = 0x00;
  act[771] = 0x03;  // transparent index 3
  const auto act_parsed = patchy::palette_io::read_palette_bytes(act);
  CHECK(act_parsed.colors.size() == colors.size());
  CHECK(act_parsed.transparent_index.has_value());
  CHECK(*act_parsed.transparent_index == 3);

  bool threw = false;
  try {
    const std::string garbage = "certainly not a palette";
    (void)patchy::palette_io::read_palette_bytes(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(garbage.data()), garbage.size()));
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(threw);
}

void palette_presets_are_well_formed() {
  const auto presets = patchy::builtin_palette_presets();
  CHECK(presets.size() == 13);

  const auto expect_count = [&presets](std::string_view id, std::size_t count) {
    const auto* preset = patchy::find_builtin_palette_preset(id);
    CHECK(preset != nullptr);
    CHECK(preset->colors.size() == count);
  };
  expect_count("nes", 54);
  expect_count("c64", 16);
  expect_count("gameboy", 4);
  expect_count("pico8", 16);
  expect_count("cga", 16);
  expect_count("ega64", 64);
  expect_count("vga256", 246);
  expect_count("zx_spectrum", 15);
  expect_count("msx", 15);
  expect_count("amstrad_cpc", 27);
  expect_count("dawnbringer16", 16);
  expect_count("dawnbringer32", 32);
  expect_count("dink", 256);

  for (const auto& preset : presets) {
    CHECK(preset.colors.size() >= 4);
    CHECK(preset.colors.size() <= 256);
    std::unordered_set<std::uint32_t> unique;
    for (const auto& color : preset.colors) {
      CHECK(unique.insert(patchy::palette_color_key(color)).second);  // presets carry no duplicate entries
    }
  }

  const auto* pico8 = patchy::find_builtin_palette_preset("pico8");
  CHECK(pico8->colors[8].red == 0xFF);
  CHECK(pico8->colors[8].green == 0x00);
  CHECK(pico8->colors[8].blue == 0x4D);
  const auto* gameboy = patchy::find_builtin_palette_preset("gameboy");
  CHECK(gameboy->colors[3].red == 0x9B);
  // VGA default DAC: EGA white bit-replicates to full white, the first wheel
  // entry after the 16 EGA colors + 14-step gray ramp is pure blue.
  const auto* vga = patchy::find_builtin_palette_preset("vga256");
  CHECK(patchy::palette_color_key(vga->colors[0]) == 0x000000U);
  CHECK(patchy::palette_color_key(vga->colors[15]) == 0xFFFFFFU);
  CHECK(patchy::palette_color_key(vga->colors[30]) == 0x0000FFU);
  // Dink Smallwood: engine index order (white first, black last, the pure-green
  // sprite key near the end).
  const auto* dink = patchy::find_builtin_palette_preset("dink");
  CHECK(patchy::palette_color_key(dink->colors[0]) == 0xFFFFFFU);
  CHECK(patchy::palette_color_key(dink->colors[252]) == 0x00FF00U);
  CHECK(patchy::palette_color_key(dink->colors[255]) == 0x000000U);
  CHECK(patchy::find_builtin_palette_preset("not_a_preset") == nullptr);
}

void tool_writes_snap_to_palette_when_constrained() {
  const auto* preset = patchy::find_builtin_palette_preset("pico8");
  CHECK(preset != nullptr);
  patchy::PaletteLut lut;
  lut.build(preset->colors);
  patchy::PaletteSnapContext snap;
  snap.lut = &lut;

  const auto check_layer_clean = [&lut](const patchy::Document& document, patchy::LayerId layer_id) {
    const auto* layer = document.find_layer(layer_id);
    CHECK(layer != nullptr);
    CHECK(patchy::pixels_are_palette_clean(layer->pixels(), lut));
  };
  const auto snapped_options = [&snap](std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    auto options = tool_options(r, g, b);
    options.palette_snap = &snap;
    return options;
  };

  {  // Soft procedural brush: hard rim, palette colors, 0/255 alpha only.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = snapped_options(211, 32, 41);
    options.brush_size = 15;
    options.brush_softness = 80;
    CHECK(!patchy::paint_brush_segment(document, layer_id, 10.0, 20.0, 44.0, 30.0, options, false).empty());
    check_layer_clean(document, layer_id);
    CHECK(document.find_layer(layer_id)->pixels().pixel(30, 25)[3] == 255);
  }
  {  // Painting with an exact palette color keeps exactly that color.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = snapped_options(0xFF, 0x00, 0x4D);  // PICO-8 entry 8
    options.brush_size = 9;
    CHECK(!patchy::paint_brush(document, layer_id, 20, 20, options, false).empty());
    const auto* px = document.find_layer(layer_id)->pixels().pixel(20, 20);
    CHECK(px[0] == 0xFF);
    CHECK(px[1] == 0x00);
    CHECK(px[2] == 0x4D);
    CHECK(px[3] == 255);
    check_layer_clean(document, layer_id);
  }
  {  // Bitmap tip stroke through the stateful spacing overload.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    const auto tip = make_bar_brush_tip();
    const auto mips = patchy::build_brush_tip_mips(tip);
    const auto scaled = patchy::make_scaled_brush_tip(mips, 9);
    auto options = snapped_options(30, 160, 220);
    options.brush_size = 9;
    options.brush_tip = &scaled;
    patchy::BrushTipStrokeState state;
    CHECK(!patchy::paint_brush_segment(document, layer_id, 10.0, 20.0, 40.0, 28.0, options, false, state).empty());
    check_layer_clean(document, layer_id);
  }
  {  // Anti-aliased shapes: filled rounded rect and ellipse outline.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto fill = snapped_options(255, 120, 0);
    fill.fill_shapes = true;
    fill.shape_corner_radius = 6;
    CHECK(!patchy::draw_rectangle(document, layer_id, patchy::Rect{8, 6, 30, 22}, fill, false).empty());
    auto outline = snapped_options(150, 40, 220);
    outline.brush_size = 3;
    CHECK(!patchy::draw_ellipse(document, layer_id, patchy::Rect{30, 20, 30, 22}, outline, false).empty());
    check_layer_clean(document, layer_id);
  }
  {  // Feathered fill inside a selection hardens at the coverage threshold.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto options = snapped_options(20, 90, 200);
    options.selection = patchy::Rect{10, 8, 30, 24};
    options.fill_softness_feather = 4.0;
    CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{10, 8, 30, 24}, options).empty());
    check_layer_clean(document, layer_id);
  }
  {  // Flood fill on the 3-channel background.
    auto document = make_tool_document();
    const auto background_id = document.layers().front().id();
    CHECK(!patchy::flood_fill(document, background_id, 5, 5, snapped_options(0, 180, 210)).empty());
    check_layer_clean(document, background_id);
  }
  {  // Gradient with off-palette, alpha-varying stops posterizes to the palette.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    patchy::GradientOptions gradient;
    gradient.method = patchy::GradientMethod::Linear;
    gradient.opacity = 0.9F;
    gradient.stops = {{0.0F, patchy::EditColor{255, 0, 0, 255}},
                      {0.5F, patchy::EditColor{40, 200, 60, 128}},
                      {1.0F, patchy::EditColor{20, 40, 255, 0}}};
    CHECK(!patchy::draw_gradient(document, layer_id, 0, 0, 63, 47, snapped_options(0, 0, 0), gradient).empty());
    check_layer_clean(document, layer_id);
  }
  {  // Soft erase keeps alpha hard; clear honors the same rule.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, snapped_options(0xFF, 0xF1, 0xE8)).empty());
    auto erase_options = snapped_options(0, 0, 0);
    erase_options.brush_size = 14;
    erase_options.brush_softness = 60;
    CHECK(!patchy::paint_brush_segment(document, layer_id, 12.0, 12.0, 40.0, 30.0, erase_options, true).empty());
    check_layer_clean(document, layer_id);
    auto clear_options = snapped_options(0, 0, 0);
    clear_options.selection = patchy::Rect{6, 30, 20, 12};
    CHECK(!patchy::clear_rect(document, layer_id, patchy::Rect{0, 0, 64, 48}, clear_options).empty());
    check_layer_clean(document, layer_id);
  }
  {  // Smudge drags palette content and still lands on palette colors.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 32, 48}, snapped_options(0xFF, 0xA3, 0x00)).empty());
    auto options = snapped_options(0, 0, 0);
    options.brush_size = 13;
    CHECK(!patchy::smudge_brush_segment(document, layer_id, 20, 20, 48, 20, options).empty());
    check_layer_clean(document, layer_id);
  }
  {  // The transparency lock is honored: pixels the blend refuses are never
     // snapped, even though their RGB is off-palette.
    auto document = make_tool_document();
    const auto layer_id = active_tool_layer(document);
    auto* px = document.find_layer(layer_id)->pixels().pixel(20, 20);
    px[0] = 7;
    px[1] = 8;
    px[2] = 9;
    px[3] = 0;
    auto options = snapped_options(0xFF, 0x00, 0x4D);
    options.lock_transparent_pixels = true;
    options.brush_size = 9;
    (void)patchy::paint_brush(document, layer_id, 20, 20, options, false);
    const auto* after = document.find_layer(layer_id)->pixels().pixel(20, 20);
    CHECK(after[0] == 7);
    CHECK(after[1] == 8);
    CHECK(after[2] == 9);
    CHECK(after[3] == 0);
  }
}

void bmp_palette_mode_doc_exports_exact_document_palette() {
  patchy::Document document(8, 4, patchy::PixelFormat::rgb8());
  const auto* preset = patchy::find_builtin_palette_preset("gameboy");
  CHECK(preset != nullptr);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(preset->colors.begin(), preset->colors.end());
  document.palette_editing() = editing;
  patchy::sync_document_indexed_palette(document);

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
  // One off-palette pixel, standing in for live layer-style output that the
  // flatten picks up; Exact export must snap it instead of failing.
  auto* off_palette = pixels.pixel(5, 2);
  off_palette[0] = 250;
  off_palette[1] = 10;
  off_palette[2] = 10;
  document.add_pixel_layer("Art", std::move(pixels));

  patchy::bmp::WriteOptions options;
  options.encoding = patchy::bmp::BmpEncoding::Indexed2;
  options.palette_mode = patchy::bmp::BmpPaletteMode::Exact;
  const auto bytes = patchy::bmp::DocumentIo::write(document, options);
  const auto read = patchy::bmp::DocumentIo::read(bytes);
  CHECK(read.indexed_palette().has_value());
  CHECK(read.indexed_palette()->colors.size() == 4);
  for (std::size_t index = 0; index < 4; ++index) {
    // The file carries the DOCUMENT palette, in order.
    CHECK(patchy::palette_color_key(read.indexed_palette()->colors[index]) ==
          patchy::palette_color_key(preset->colors[index]));
  }
  patchy::PaletteLut lut;
  lut.build(preset->colors);
  const auto* snapped = read.layers().front().pixels().pixel(5, 2);
  CHECK(lut.contains(patchy::RgbColor{snapped[0], snapped[1], snapped[2]}));
}

void psd_round_trips_palette_resource() {
  auto document = make_tool_document();
  const auto* preset = patchy::find_builtin_palette_preset("gameboy");
  CHECK(preset != nullptr);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(preset->colors.begin(), preset->colors.end());
  editing.alpha_threshold = 96;
  document.palette_editing() = editing;
  patchy::sync_document_indexed_palette(document);

  // The layered writer carries the palette resource; the mode flag survives.
  const auto layered = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto reread = patchy::psd::DocumentIo::read(layered);
  CHECK(reread.palette_editing().has_value());
  CHECK(reread.palette_editing()->palette.colors.size() == 4);
  CHECK(reread.palette_editing()->alpha_threshold == 96);
  CHECK(reread.palette_editing()->palette.colors[3].red == 0x9B);
  CHECK(reread.indexed_palette().has_value());

  // An attached palette without the editing mode also travels, mode off.
  document.palette_editing().reset();
  const auto rgb_reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_flat_rgb8(document));
  CHECK(!rgb_reread.palette_editing().has_value());
  CHECK(rgb_reread.indexed_palette().has_value());
  CHECK(rgb_reread.indexed_palette()->colors.size() == 4);

  // No palette anywhere: no resource, nothing restored.
  auto plain = make_tool_document();
  const auto plain_reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_flat_rgb8(plain));
  CHECK(!plain_reread.palette_editing().has_value());
  CHECK(!plain_reread.indexed_palette().has_value());

  // A corrupt payload is ignored: the file opens as plain RGB. Flip the magic in
  // the written bytes and re-read.
  auto corrupted = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const std::array<std::uint8_t, 4> magic = {'P', 't', 'c', 'P'};
  for (std::size_t offset = 0; offset + magic.size() <= corrupted.size(); ++offset) {
    if (std::equal(magic.begin(), magic.end(), corrupted.begin() + static_cast<std::ptrdiff_t>(offset))) {
      corrupted[offset] = 'X';
      break;
    }
  }
  const auto corrupt_reread = patchy::psd::DocumentIo::read(corrupted);
  CHECK(!corrupt_reread.palette_editing().has_value());
}

void document_palette_editing_copies_and_syncs_indexed_mirror() {
  patchy::Document document(4, 4, patchy::PixelFormat::rgb8());
  CHECK(!document.palette_editing().has_value());

  patchy::DocumentPaletteEditing editing;
  editing.palette.colors = {{0, 0, 0}, {255, 255, 255}, {255, 0, 0}};
  editing.alpha_threshold = 100;
  editing.palette_revision = 7;
  document.palette_editing() = editing;

  // Undo snapshots copy the whole Document; palette state must ride along.
  patchy::Document snapshot = document;
  document.palette_editing()->palette.colors.push_back({0, 255, 0});
  document.palette_editing()->palette_revision = 8;
  CHECK(snapshot.palette_editing().has_value());
  CHECK(snapshot.palette_editing()->palette.colors.size() == 3);
  CHECK(snapshot.palette_editing()->palette_revision == 7);
  CHECK(document.palette_editing()->palette.colors.size() == 4);

  patchy::sync_document_indexed_palette(document);
  CHECK(document.indexed_palette().has_value());
  CHECK(document.indexed_palette()->colors.size() == 4);
  CHECK(document.indexed_palette()->source_bit_depth == 2);

  document.palette_editing()->palette.colors.resize(17, patchy::RgbColor{1, 2, 3});
  patchy::sync_document_indexed_palette(document);
  CHECK(document.indexed_palette()->source_bit_depth == 8);

  patchy::Document untouched(2, 2, patchy::PixelFormat::rgb8());
  untouched.indexed_palette() = patchy::DocumentIndexedPalette{{{9, 9, 9}}, 8};
  patchy::sync_document_indexed_palette(untouched);  // no editing state: mirror keeps import metadata
  CHECK(untouched.indexed_palette()->colors.size() == 1);
  CHECK(untouched.indexed_palette()->colors[0].red == 9);
}

void tool_flip_horizontal_changes_pixels_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(255, 0, 0);
  CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 8, 48}, options).empty());
  CHECK(!patchy::flip_layer_horizontal(document, layer_id).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(63, 10)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(0, 10)[3] == 0);
  write_bmp_artifact("tool_flip_horizontal", document);
}

void tool_flip_vertical_changes_pixels_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(0, 0, 255);
  CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 64, 8}, options).empty());
  CHECK(!patchy::flip_layer_vertical(document, layer_id).empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(10, 47)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(10, 0)[3] == 0);
  write_bmp_artifact("tool_flip_vertical", document);
}

void document_crop_to_selection_changes_canvas_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(255, 0, 180);
  CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{12, 8, 4, 4}, options).empty());
  CHECK(patchy::crop_document(document, patchy::Rect{8, 6, 32, 20}));
  CHECK(document.width() == 32);
  CHECK(document.height() == 20);
  const auto* px = document.find_layer(layer_id)->pixels().pixel(4, 2);
  CHECK(px[0] == 255);
  CHECK(px[1] == 0);
  CHECK(px[2] == 180);
  CHECK(px[3] == 255);
  write_bmp_artifact("document_crop", document);
}

void document_canvas_resize_expands_layers_for_editing() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(10, 90, 220);
  CHECK(!patchy::paint_brush(document, layer_id, 20, 20, options, false).empty());

  patchy::resize_canvas_and_layers(document, 96, 72);
  CHECK(document.width() == 96);
  CHECK(document.height() == 72);
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  CHECK(layer->bounds().x == 0);
  CHECK(layer->bounds().y == 0);
  CHECK(layer->pixels().width() == 96);
  CHECK(layer->pixels().height() == 72);
  CHECK(layer->pixels().pixel(20, 20)[3] == 255);

  CHECK(!patchy::paint_brush(document, layer_id, 90, 66, options, false).empty());
  CHECK(layer->pixels().pixel(90, 66)[3] == 255);
  write_bmp_artifact("document_canvas_resize", document);
}

void document_canvas_resize_honors_anchor_and_extension_color() {
  patchy::Document document(4, 4, patchy::PixelFormat::rgb8());
  const auto& background = document.add_pixel_layer("Background", solid_rgb(4, 4, 255, 255, 255));
  const auto background_id = background.id();
  patchy::Layer sticker(document.allocate_layer_id(), "Sticker", solid_rgba(1, 1, 220, 10, 90, 255));
  const auto sticker_id = sticker.id();
  sticker.set_bounds(patchy::Rect{1, 1, 1, 1});
  document.add_layer(std::move(sticker));

  patchy::resize_canvas_and_layers(document, 6, 6, patchy::CanvasAnchor::Center,
                                      patchy::EditColor{12, 34, 56, 255});
  CHECK(document.width() == 6);
  CHECK(document.height() == 6);

  const auto* background_layer = document.find_layer(background_id);
  CHECK(background_layer != nullptr);
  CHECK(background_layer->pixels().pixel(0, 0)[0] == 12);
  CHECK(background_layer->pixels().pixel(0, 0)[1] == 34);
  CHECK(background_layer->pixels().pixel(0, 0)[2] == 56);
  CHECK(background_layer->pixels().pixel(1, 1)[0] == 255);

  const auto* sticker_layer = document.find_layer(sticker_id);
  CHECK(sticker_layer != nullptr);
  CHECK(sticker_layer->bounds().x == 0);
  CHECK(sticker_layer->bounds().y == 0);
  CHECK(sticker_layer->pixels().pixel(1, 1)[3] == 0);
  CHECK(sticker_layer->pixels().pixel(2, 2)[0] == 220);
  CHECK(sticker_layer->pixels().pixel(2, 2)[1] == 10);
  CHECK(sticker_layer->pixels().pixel(2, 2)[2] == 90);
  CHECK(sticker_layer->pixels().pixel(2, 2)[3] == 255);
}

void document_image_resize_scales_layers_and_writes_artifact() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 48, 255, 255, 255));
  patchy::Layer sticker(document.allocate_layer_id(), "Sticker", solid_rgba(4, 4, 230, 20, 150, 255));
  const auto sticker_id = sticker.id();
  sticker.set_bounds(patchy::Rect{10, 5, 4, 4});
  document.add_layer(std::move(sticker));

  patchy::resize_image_and_layers(document, 128, 96);
  CHECK(document.width() == 128);
  CHECK(document.height() == 96);
  const auto* layer = document.find_layer(sticker_id);
  CHECK(layer != nullptr);
  CHECK(layer->bounds().x == 20);
  CHECK(layer->bounds().y == 10);
  CHECK(layer->bounds().width == 8);
  CHECK(layer->bounds().height == 8);
  CHECK(layer->pixels().width() == 8);
  CHECK(layer->pixels().height() == 8);
  const auto* px = layer->pixels().pixel(4, 4);
  CHECK(px[0] == 230);
  CHECK(px[1] == 20);
  CHECK(px[2] == 150);
  CHECK(px[3] == 255);
  write_bmp_artifact("document_image_resize", document);
}

void document_rotate_clockwise_changes_canvas_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(255, 120, 0);
  CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 8, 6}, options).empty());
  patchy::rotate_document_clockwise(document);
  CHECK(document.width() == 48);
  CHECK(document.height() == 64);
  CHECK(document.find_layer(layer_id)->pixels().pixel(47, 0)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(0, 0)[3] == 0);
  write_bmp_artifact("document_rotate_clockwise", document);
}

void document_rotate_counterclockwise_changes_canvas_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(40, 180, 255);
  CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{0, 0, 8, 6}, options).empty());
  patchy::rotate_document_counterclockwise(document);
  CHECK(document.width() == 48);
  CHECK(document.height() == 64);
  CHECK(document.find_layer(layer_id)->pixels().pixel(0, 63)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(47, 0)[3] == 0);
  write_bmp_artifact("document_rotate_counterclockwise", document);
}

void tool_stroke_selection_draws_border_and_writes_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(20, 20, 20);
  options.brush_size = 3;
  options.selection = patchy::Rect{14, 10, 30, 22};
  const auto dirty = patchy::draw_rectangle(document, layer_id, *options.selection, options, false);
  CHECK(!dirty.empty());
  CHECK(document.find_layer(layer_id)->pixels().pixel(14, 10)[3] == 255);
  CHECK(document.find_layer(layer_id)->pixels().pixel(20, 16)[3] == 0);
  write_bmp_artifact("tool_stroke_selection", document);
}

void layer_merge_visible_creates_flattened_artifact() {
  auto document = make_tool_document();
  const auto layer_id = active_tool_layer(document);
  auto options = tool_options(0, 120, 255);
  CHECK(!patchy::fill_rect(document, layer_id, patchy::Rect{4, 4, 24, 18}, options).empty());
  auto merged_pixels = patchy::Compositor{}.flatten_rgb8(document);
  document.add_pixel_layer("Merged Visible", std::move(merged_pixels));
  CHECK(document.layers().size() == 3);
  CHECK(document.layers().back().pixels().format() == patchy::PixelFormat::rgb8());
  CHECK(document.layers().back().pixels().pixel(5, 5)[2] == 255);
  write_bmp_artifact("layer_merge_visible", document);
}

void filters_register_and_apply() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  CHECK(registry.find("patchy.filters.invert") != nullptr);
  CHECK(registry.find("patchy.filters.brightness_contrast") != nullptr);
  CHECK(registry.find("patchy.filters.brightness_plus") == nullptr);
  CHECK(registry.find("patchy.filters.contrast_plus") == nullptr);
  CHECK(registry.find("patchy.filters.grayscale") != nullptr);
  CHECK(registry.find("patchy.filters.desaturate") != nullptr);
  CHECK(registry.find("patchy.filters.auto_contrast") != nullptr);
  CHECK(registry.find("patchy.filters.soft_glow") != nullptr);
  CHECK(registry.find("patchy.filters.punchy_color") != nullptr);
  CHECK(registry.find("patchy.filters.noir") != nullptr);
  CHECK(registry.find("patchy.filters.cinematic_matte") != nullptr);
  CHECK(registry.find("patchy.filters.vintage_fade") != nullptr);
  CHECK(registry.find("patchy.filters.sepia") != nullptr);
  CHECK(registry.find("patchy.filters.threshold") != nullptr);
  CHECK(registry.find("patchy.filters.posterize") != nullptr);
  CHECK(registry.find("patchy.filters.box_blur") != nullptr);
  CHECK(registry.find("patchy.filters.sharpen") != nullptr);
  CHECK(registry.find("patchy.filters.unsharp_mask") != nullptr);
  CHECK(registry.find("patchy.filters.gaussian_blur") != nullptr);
  CHECK(registry.find("patchy.filters.motion_blur") != nullptr);
  CHECK(registry.find("patchy.filters.radial_blur") != nullptr);
  CHECK(registry.find("patchy.filters.edge_detect") != nullptr);
  CHECK(registry.find("patchy.filters.emboss") != nullptr);
  CHECK(registry.find("patchy.filters.glowing_edges") != nullptr);
  CHECK(registry.find("patchy.filters.twirl") != nullptr);
  CHECK(registry.find("patchy.filters.wave") != nullptr);
  CHECK(registry.find("patchy.filters.pinch_bloat") != nullptr);
  CHECK(registry.find("patchy.filters.clouds") != nullptr);
  CHECK(registry.find("patchy.filters.pixelate") != nullptr);
  CHECK(registry.find("patchy.filters.color_halftone") != nullptr);
  CHECK(registry.find("patchy.filters.film_grain") != nullptr);
  CHECK(registry.find("patchy.filters.vignette") != nullptr);

  auto pixels = solid_rgb(1, 1, 1, 2, 3);
  registry.apply("patchy.filters.invert", pixels);
  const auto* px = pixels.pixel(0, 0);
  CHECK(px[0] == 254);
  CHECK(px[1] == 253);
  CHECK(px[2] == 252);
}

void filters_builtin_effects_apply_and_write_artifacts() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);

  const std::vector<std::pair<std::string, std::string>> filters = {
      {"patchy.filters.brightness_contrast", "filter_brightness_contrast"},
      {"patchy.filters.grayscale", "filter_grayscale"},
      {"patchy.filters.desaturate", "filter_desaturate"},
      {"patchy.filters.auto_contrast", "filter_auto_contrast"},
      {"patchy.filters.soft_glow", "filter_soft_glow"},
      {"patchy.filters.punchy_color", "filter_punchy_color"},
      {"patchy.filters.noir", "filter_noir"},
      {"patchy.filters.cinematic_matte", "filter_cinematic_matte"},
      {"patchy.filters.vintage_fade", "filter_vintage_fade"},
      {"patchy.filters.sepia", "filter_sepia"},
      {"patchy.filters.threshold", "filter_threshold"},
      {"patchy.filters.posterize", "filter_posterize"},
      {"patchy.filters.box_blur", "filter_box_blur"},
      {"patchy.filters.sharpen", "filter_sharpen"},
      {"patchy.filters.unsharp_mask", "filter_unsharp_mask"},
      {"patchy.filters.gaussian_blur", "filter_gaussian_blur"},
      {"patchy.filters.motion_blur", "filter_motion_blur"},
      {"patchy.filters.radial_blur", "filter_radial_blur"},
      {"patchy.filters.edge_detect", "filter_edge_detect"},
      {"patchy.filters.emboss", "filter_emboss"},
      {"patchy.filters.glowing_edges", "filter_glowing_edges"},
      {"patchy.filters.twirl", "filter_twirl"},
      {"patchy.filters.wave", "filter_wave"},
      {"patchy.filters.pinch_bloat", "filter_pinch_bloat"},
      {"patchy.filters.clouds", "filter_clouds"},
      {"patchy.filters.pixelate", "filter_pixelate"},
      {"patchy.filters.color_halftone", "filter_color_halftone"},
      {"patchy.filters.film_grain", "filter_film_grain"},
      {"patchy.filters.vignette", "filter_vignette"},
  };

  for (const auto& [identifier, artifact_name] : filters) {
    auto document = make_filter_document();
    auto& pixels = document.layers().front().pixels();
    registry.apply(identifier, pixels);
    CHECK(!pixels.empty());
    write_bmp_artifact(artifact_name, document);
  }

  auto brightness_contrast = make_filter_document();
  const auto* original_brightness_contrast_px = brightness_contrast.layers().front().pixels().pixel(0, 0);
  const auto original_brightness_contrast_red = original_brightness_contrast_px[0];
  const auto original_brightness_contrast_green = original_brightness_contrast_px[1];
  const auto original_brightness_contrast_blue = original_brightness_contrast_px[2];
  registry.apply("patchy.filters.brightness_contrast", brightness_contrast.layers().front().pixels());
  const auto* brightness_contrast_px = brightness_contrast.layers().front().pixels().pixel(0, 0);
  CHECK(brightness_contrast_px[0] == original_brightness_contrast_red);
  CHECK(brightness_contrast_px[1] == original_brightness_contrast_green);
  CHECK(brightness_contrast_px[2] == original_brightness_contrast_blue);

  auto threshold = make_filter_document();
  registry.apply("patchy.filters.threshold", threshold.layers().front().pixels());
  const auto* threshold_px = threshold.layers().front().pixels().pixel(0, 0);
  CHECK(threshold_px[0] == 0);
  CHECK(threshold_px[1] == 0);
  CHECK(threshold_px[2] == 0);

  auto desaturate = make_filter_document();
  registry.apply("patchy.filters.desaturate", desaturate.layers().front().pixels());
  const auto* desaturated_px = desaturate.layers().front().pixels().pixel(3, 2);
  CHECK(desaturated_px[0] == desaturated_px[1]);
  CHECK(desaturated_px[1] == desaturated_px[2]);

  auto auto_contrast = make_filter_document();
  registry.apply("patchy.filters.auto_contrast", auto_contrast.layers().front().pixels());
  const auto* low_px = auto_contrast.layers().front().pixels().pixel(0, 0);
  const auto* high_px = auto_contrast.layers().front().pixels().pixel(31, 23);
  CHECK(low_px[0] == 0);
  CHECK(low_px[1] == 0);
  CHECK(low_px[2] == 0);
  CHECK(high_px[0] == 255);
  CHECK(high_px[1] == 255);
  CHECK(high_px[2] == 255);

  auto noir = make_filter_document();
  registry.apply("patchy.filters.noir", noir.layers().front().pixels());
  const auto* noir_px = noir.layers().front().pixels().pixel(10, 10);
  CHECK(noir_px[0] == noir_px[1]);
  CHECK(noir_px[1] == noir_px[2]);

  auto pin_blur = patchy::PixelBuffer(5, 5, patchy::PixelFormat::rgb8());
  pin_blur.pixel(2, 2)[0] = 255;
  pin_blur.pixel(2, 2)[1] = 255;
  pin_blur.pixel(2, 2)[2] = 255;
  registry.apply("patchy.filters.gaussian_blur", pin_blur);
  CHECK(pin_blur.pixel(2, 2)[0] > pin_blur.pixel(1, 2)[0]);
  CHECK(pin_blur.pixel(1, 2)[0] > 0);
  CHECK(pin_blur.pixel(2, 2)[0] < 255);

  auto unsharp = solid_rgb(5, 1, 40, 40, 40);
  for (std::int32_t x = 2; x < unsharp.width(); ++x) {
    auto* px = unsharp.pixel(x, 0);
    px[0] = 160;
    px[1] = 160;
    px[2] = 160;
  }
  registry.apply("patchy.filters.unsharp_mask", unsharp);
  CHECK(unsharp.pixel(1, 0)[0] < 40);
  CHECK(unsharp.pixel(2, 0)[0] > 160);

  auto motion = solid_rgb(31, 1, 0, 0, 0);
  motion.pixel(15, 0)[0] = 255;
  motion.pixel(15, 0)[1] = 255;
  motion.pixel(15, 0)[2] = 255;
  registry.apply("patchy.filters.motion_blur", motion);
  CHECK(motion.pixel(15, 0)[0] < 255);
  CHECK(motion.pixel(4, 0)[0] > 0);
  CHECK(motion.pixel(26, 0)[0] > 0);

  const auto make_transparent_red_stroke = [] {
    auto pixels = solid_rgba(65, 65, 0, 0, 0, 0);
    for (std::int32_t y = 10; y <= 54; ++y) {
      for (std::int32_t x = 43; x <= 45; ++x) {
        auto* px = pixels.pixel(x, y);
        px[0] = 230;
        px[1] = 20;
        px[2] = 20;
        px[3] = 255;
      }
    }
    for (std::int32_t i = 0; i < 30; ++i) {
      auto* px = pixels.pixel(16 + i, 42 - i / 2);
      px[0] = 230;
      px[1] = 20;
      px[2] = 20;
      px[3] = 255;
    }
    return pixels;
  };
  const auto check_transparent_spatial_filter = [&](std::string_view identifier) {
    const auto before = make_transparent_red_stroke();
    auto after = before;
    registry.apply(identifier, after);
    int spread_pixels = 0;
    bool kept_clean_red = false;
    for (std::int32_t y = 0; y < after.height(); ++y) {
      for (std::int32_t x = 0; x < after.width(); ++x) {
        const auto* src = before.pixel(x, y);
        const auto* dst = after.pixel(x, y);
        if (src[3] == 0 && dst[3] > 8) {
          ++spread_pixels;
          kept_clean_red = kept_clean_red || (dst[0] > 180 && dst[1] < 80 && dst[2] < 80);
        }
      }
    }
    CHECK(spread_pixels > 0);
    CHECK(kept_clean_red);
  };
  check_transparent_spatial_filter("patchy.filters.box_blur");
  check_transparent_spatial_filter("patchy.filters.gaussian_blur");
  check_transparent_spatial_filter("patchy.filters.motion_blur");
  check_transparent_spatial_filter("patchy.filters.radial_blur");
  check_transparent_spatial_filter("patchy.filters.pixelate");
  auto transparent_clouds = solid_rgba(16, 16, 0, 0, 0, 0);
  registry.apply("patchy.filters.clouds", transparent_clouds);
  CHECK(transparent_clouds.pixel(0, 0)[3] == 255);
  CHECK(transparent_clouds.pixel(15, 15)[3] == 255);

  auto edge = solid_rgb(3, 3, 0, 0, 0);
  for (std::int32_t y = 0; y < edge.height(); ++y) {
    for (std::int32_t x = 1; x < edge.width(); ++x) {
      auto* px = edge.pixel(x, y);
      px[0] = 255;
      px[1] = 255;
      px[2] = 255;
    }
  }
  registry.apply("patchy.filters.edge_detect", edge);
  CHECK(edge.pixel(1, 1)[0] == 255);
  CHECK(edge.pixel(1, 1)[1] == 255);
  CHECK(edge.pixel(1, 1)[2] == 255);

  auto glowing = solid_rgb(3, 3, 0, 0, 0);
  for (std::int32_t y = 0; y < glowing.height(); ++y) {
    for (std::int32_t x = 1; x < glowing.width(); ++x) {
      auto* px = glowing.pixel(x, y);
      px[0] = 255;
      px[1] = 255;
      px[2] = 255;
    }
  }
  registry.apply("patchy.filters.glowing_edges", glowing);
  CHECK(glowing.pixel(1, 1)[0] > 0);
  CHECK(glowing.pixel(0, 1)[0] < glowing.pixel(1, 1)[0]);

  auto relief = solid_rgb(3, 3, 100, 110, 120);
  registry.apply("patchy.filters.emboss", relief);
  CHECK(relief.pixel(1, 1)[0] == 128);
  CHECK(relief.pixel(1, 1)[1] == 128);
  CHECK(relief.pixel(1, 1)[2] == 128);

  auto twirled = make_filter_document();
  const auto twirl_before = twirled.layers().front().pixels();
  registry.apply("patchy.filters.twirl", twirled.layers().front().pixels());
  const auto twirl_after_data = twirled.layers().front().pixels().data();
  const auto twirl_before_data = twirl_before.data();
  CHECK(!std::equal(twirl_after_data.begin(), twirl_after_data.end(), twirl_before_data.begin()));

  auto wave = patchy::PixelBuffer(64, 16, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < wave.height(); ++y) {
    for (std::int32_t x = 0; x < wave.width(); ++x) {
      auto* px = wave.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x * 3);
      px[1] = 0;
      px[2] = 0;
    }
  }
  const auto wave_before = wave.pixel(10, 12)[0];
  registry.apply("patchy.filters.wave", wave);
  CHECK(wave.pixel(10, 12)[0] > wave_before);

  auto pinch = patchy::PixelBuffer(33, 33, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < pinch.height(); ++y) {
    for (std::int32_t x = 0; x < pinch.width(); ++x) {
      auto* px = pinch.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x * 6);
      px[1] = 0;
      px[2] = 0;
    }
  }
  const auto pinch_before = pinch.pixel(20, 16)[0];
  registry.apply("patchy.filters.pinch_bloat", pinch);
  CHECK(pinch.pixel(20, 16)[0] < pinch_before);

  auto clouds = make_filter_document();
  registry.apply("patchy.filters.clouds", clouds.layers().front().pixels());
  const auto* cloud_a = clouds.layers().front().pixels().pixel(0, 0);
  const auto* cloud_b = clouds.layers().front().pixels().pixel(31, 23);
  CHECK(cloud_a[0] == cloud_a[1]);
  CHECK(cloud_a[1] == cloud_a[2]);
  CHECK(cloud_a[0] != cloud_b[0]);

  auto pixelated = make_filter_document();
  registry.apply("patchy.filters.pixelate", pixelated.layers().front().pixels());
  const auto* pixelated_px = pixelated.layers().front().pixels().pixel(0, 0);
  CHECK(pixelated_px[0] == 12);
  CHECK(pixelated_px[1] == 15);
  CHECK(pixelated_px[2] == 83);

  auto halftone = solid_rgb(24, 24, 128, 128, 128);
  registry.apply("patchy.filters.color_halftone", halftone);
  bool halftone_varied = false;
  const auto* halftone_first = halftone.pixel(0, 0);
  for (std::int32_t y = 0; y < halftone.height(); ++y) {
    for (std::int32_t x = 0; x < halftone.width(); ++x) {
      const auto* px = halftone.pixel(x, y);
      halftone_varied = halftone_varied || px[0] != halftone_first[0] || px[1] != halftone_first[1] ||
                        px[2] != halftone_first[2];
    }
  }
  CHECK(halftone_varied);

  auto grain_a = solid_rgb(2, 2, 128, 128, 128);
  auto grain_b = solid_rgb(2, 2, 128, 128, 128);
  registry.apply("patchy.filters.film_grain", grain_a);
  registry.apply("patchy.filters.film_grain", grain_b);
  bool grain_changed = false;
  for (std::int32_t y = 0; y < grain_a.height(); ++y) {
    for (std::int32_t x = 0; x < grain_a.width(); ++x) {
      const auto* a = grain_a.pixel(x, y);
      const auto* b = grain_b.pixel(x, y);
      for (std::uint16_t channel = 0; channel < 3; ++channel) {
        CHECK(a[channel] == b[channel]);
        grain_changed = grain_changed || a[channel] != 128;
      }
    }
  }
  CHECK(grain_changed);

  auto vignetted = solid_rgb(5, 5, 255, 255, 255);
  registry.apply("patchy.filters.vignette", vignetted);
  CHECK(vignetted.pixel(2, 2)[0] == 255);
  CHECK(vignetted.pixel(0, 0)[0] < 130);
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

void plugin_host_and_legacy_probe_work() {
  patchy::PluginHost host;
  host.register_plugin({PATCHY_PLUGIN_FILTER, "com.patchy.test", "Test", 1, 0, 0, {}});
  CHECK(host.find("com.patchy.test") != nullptr);
  CHECK(host.plugins_by_kind(PATCHY_PLUGIN_FILTER).size() == 1);

  const auto fixture = std::filesystem::path("test_sample_filter.8bf");
  {
    std::vector<std::uint8_t> bytes(512, 0);
    bytes[0] = 'M';
    bytes[1] = 'Z';
    bytes[0x3c] = 0x80;
    bytes[0x80] = 'P';
    bytes[0x81] = 'E';
#if defined(_M_IX86) || defined(__i386__)
    bytes[0x84] = 0x4c;
    bytes[0x85] = 0x01;
#elif defined(_M_ARM64) || defined(__aarch64__)
    bytes[0x84] = 0x64;
    bytes[0x85] = 0xaa;
#else
    bytes[0x84] = 0x64;
    bytes[0x85] = 0x86;
#endif
    std::ofstream output(fixture, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }

  patchy::LegacyPhotoshopAdapter adapter;
  const auto probe = adapter.probe(fixture);
#ifdef _WIN32
  CHECK(probe.supported);
#else
  // Legacy plug-ins are Windows PE binaries; off-Windows the probe rejects them with a
  // platform reason no matter how well the architecture matches the host.
  CHECK(!probe.supported);
#endif
  CHECK(probe.kind == patchy::LegacyPhotoshopPluginKind::Filter8bf);
  CHECK(!probe.architecture.empty());
  std::filesystem::remove(fixture);

#ifdef PATCHY_SOURCE_DIR
  const auto real_plugin =
      std::filesystem::path(PATCHY_SOURCE_DIR) / "test-fixtures" / "photoshop-plugins" / "Greyscale64.8bf";
  CHECK(std::filesystem::exists(real_plugin));
  const auto real_probe = adapter.probe(real_plugin);
  CHECK(real_probe.kind == patchy::LegacyPhotoshopPluginKind::Filter8bf);
  CHECK(real_probe.architecture == "x64");
#endif
}

void tile_cache_stores_and_invalidates() {
  patchy::TileCache cache(128);
  patchy::TileKey key{0, 0, 0};
  cache.put(key, solid_rgb(2, 2, 9, 8, 7));
  CHECK(cache.find(key).has_value());
  cache.invalidate(key);
  CHECK(!cache.find(key).has_value());
}

void color_manager_assigns_profiles() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  patchy::ColorManager manager;
  manager.assign_icc_profile(document, {1, 2, 3});
  CHECK(document.color_state().embedded_icc_profile.size() == 3);
}

// ---------------------------------------------------------------------------
// Quick Select
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> quick_select_image(std::int32_t width, std::int32_t height,
                                             std::array<std::uint8_t, 4> rgba) {
  std::vector<std::uint8_t> image(static_cast<std::size_t>(width) * height * 4);
  for (std::size_t i = 0; i < image.size(); i += 4) {
    image[i] = rgba[0];
    image[i + 1] = rgba[1];
    image[i + 2] = rgba[2];
    image[i + 3] = rgba[3];
  }
  return image;
}

void quick_select_fill_rect(std::vector<std::uint8_t>& image, std::int32_t width, patchy::Rect rect,
                            std::array<std::uint8_t, 4> rgba) {
  for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
    for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
      auto* px = image.data() + (static_cast<std::size_t>(y) * width + x) * 4;
      px[0] = rgba[0];
      px[1] = rgba[1];
      px[2] = rgba[2];
      px[3] = rgba[3];
    }
  }
}

void quick_select_fill_mask(std::vector<std::uint8_t>& mask, std::int32_t width, patchy::Rect rect,
                            std::uint8_t value) {
  for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
    std::fill_n(mask.data() + static_cast<std::size_t>(y) * width + rect.x,
                static_cast<std::size_t>(rect.width), value);
  }
}

void quick_select_apply_delta(std::vector<std::uint8_t>& selection, std::int32_t width,
                              const patchy::QuickSelectResult& result, bool subtract) {
  for (const auto& run : result.delta_runs) {
    for (std::int32_t x = run.x0; x <= run.x1; ++x) {
      selection[static_cast<std::size_t>(run.y) * width + x] = subtract ? 0 : 255;
    }
  }
}

int quick_select_count_in_rect(const std::vector<std::uint8_t>& mask, std::int32_t width, patchy::Rect rect) {
  int count = 0;
  for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
    for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
      count += mask[static_cast<std::size_t>(y) * width + x] != 0 ? 1 : 0;
    }
  }
  return count;
}

void quick_select_maxflow_solves_tiny_grid() {
  // Three nodes in a row: source-(5)->n0 -(2)- n1 -(4)- n2 -(5)->sink. The only path
  // bottlenecks on the 2-capacity arc, so flow = 2 and the cut separates n0 from n1/n2.
  patchy::detail::GridMaxflow graph(3, 1);
  graph.set_terminal_caps(0, 5.0f, 0.0f);
  graph.set_terminal_caps(2, 0.0f, 5.0f);
  graph.set_neighbor_cap(0, 1, 2.0f);
  graph.set_neighbor_cap(1, 2, 4.0f);
  const double flow = graph.solve();
  CHECK(std::abs(flow - 2.0) < 1e-6);
  CHECK(graph.is_source_side(0));
  CHECK(!graph.is_source_side(1));
  CHECK(!graph.is_source_side(2));
}

// Reference max-flow (Edmonds-Karp over an explicit capacity matrix) used to validate the
// Boykov-Kolmogorov implementation on small random grids.
double quick_select_reference_maxflow(std::vector<std::vector<double>> capacity, int source, int sink) {
  const int node_count = static_cast<int>(capacity.size());
  double flow = 0.0;
  while (true) {
    std::vector<int> parent(node_count, -1);
    parent[source] = source;
    std::vector<int> queue{source};
    for (std::size_t head = 0; head < queue.size() && parent[sink] < 0; ++head) {
      const int node = queue[head];
      for (int next = 0; next < node_count; ++next) {
        if (parent[next] < 0 && capacity[node][next] > 1e-12) {
          parent[next] = node;
          queue.push_back(next);
        }
      }
    }
    if (parent[sink] < 0) {
      return flow;
    }
    double bottleneck = std::numeric_limits<double>::max();
    for (int node = sink; node != source; node = parent[node]) {
      bottleneck = std::min(bottleneck, capacity[parent[node]][node]);
    }
    for (int node = sink; node != source; node = parent[node]) {
      capacity[parent[node]][node] -= bottleneck;
      capacity[node][parent[node]] += bottleneck;
    }
    flow += bottleneck;
  }
}

void quick_select_maxflow_matches_reference_on_random_grids() {
  std::uint32_t rng_state = 0xC0FFEE01u;
  const auto next_random = [&rng_state]() {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
  };
  for (int round = 0; round < 60; ++round) {
    const std::int32_t width = 2 + static_cast<std::int32_t>(next_random() % 4);   // 2..5
    const std::int32_t height = 2 + static_cast<std::int32_t>(next_random() % 4);  // 2..5
    const int nodes = width * height;
    const int source = nodes;
    const int sink = nodes + 1;
    std::vector<std::vector<double>> capacity(static_cast<std::size_t>(nodes) + 2,
                                              std::vector<double>(static_cast<std::size_t>(nodes) + 2, 0.0));
    patchy::detail::GridMaxflow graph(width, height);

    std::vector<double> source_caps(static_cast<std::size_t>(nodes));
    std::vector<double> sink_caps(static_cast<std::size_t>(nodes));
    for (int node = 0; node < nodes; ++node) {
      // Integer-valued capacities keep every intermediate float sum exact.
      const auto source_cap = static_cast<double>(next_random() % 11);
      const auto sink_cap = static_cast<double>(next_random() % 11);
      source_caps[static_cast<std::size_t>(node)] = source_cap;
      sink_caps[static_cast<std::size_t>(node)] = sink_cap;
      graph.set_terminal_caps(node, static_cast<float>(source_cap), static_cast<float>(sink_cap));
      capacity[static_cast<std::size_t>(source)][static_cast<std::size_t>(node)] = source_cap;
      capacity[static_cast<std::size_t>(node)][static_cast<std::size_t>(sink)] = sink_cap;
    }
    const auto connect = [&](int a, int b) {
      const auto cap = static_cast<double>(next_random() % 11);
      graph.set_neighbor_cap(a, b, static_cast<float>(cap));
      capacity[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)] = cap;
      capacity[static_cast<std::size_t>(b)][static_cast<std::size_t>(a)] = cap;
    };
    for (std::int32_t y = 0; y < height; ++y) {
      for (std::int32_t x = 0; x < width; ++x) {
        const int node = y * width + x;
        if (x + 1 < width) {
          connect(node, node + 1);
        }
        if (y + 1 < height) {
          connect(node, node + width);
          if (x + 1 < width) {
            connect(node, node + width + 1);
          }
          if (x > 0) {
            connect(node, node + width - 1);
          }
        }
      }
    }

    const double expected = quick_select_reference_maxflow(capacity, source, sink);
    const double flow = graph.solve();
    CHECK(std::abs(flow - expected) < 1e-3);

    // The labeling must describe a cut whose value equals the max flow (min-cut duality).
    double cut = 0.0;
    for (int node = 0; node < nodes; ++node) {
      if (graph.is_source_side(node)) {
        cut += sink_caps[static_cast<std::size_t>(node)];
      } else {
        cut += source_caps[static_cast<std::size_t>(node)];
      }
    }
    for (std::int32_t y = 0; y < height; ++y) {
      for (std::int32_t x = 0; x < width; ++x) {
        const int node = y * width + x;
        const auto arc_across = [&](int other) {
          if (graph.is_source_side(node) && !graph.is_source_side(other)) {
            cut += capacity[static_cast<std::size_t>(node)][static_cast<std::size_t>(other)];
          } else if (!graph.is_source_side(node) && graph.is_source_side(other)) {
            cut += capacity[static_cast<std::size_t>(other)][static_cast<std::size_t>(node)];
          }
        };
        if (x + 1 < width) {
          arc_across(node + 1);
        }
        if (y + 1 < height) {
          arc_across(node + width);
          if (x + 1 < width) {
            arc_across(node + width + 1);
          }
          if (x > 0) {
            arc_across(node + width - 1);
          }
        }
      }
    }
    CHECK(std::abs(cut - expected) < 1e-3);
  }
}

void quick_select_stroke_grabs_flat_region_and_respects_edges() {
  const std::int32_t width = 200;
  const std::int32_t height = 150;
  const patchy::Rect object{60, 40, 80, 70};
  auto image = quick_select_image(width, height, {255, 255, 255, 255});
  quick_select_fill_rect(image, width, object, {200, 30, 20, 255});
  // Scatter background noise the segmentation must not leak through.
  std::uint32_t noise_state = 0xBADD5EEDu;
  for (int i = 0; i < 30; ++i) {
    noise_state ^= noise_state << 13;
    noise_state ^= noise_state >> 17;
    noise_state ^= noise_state << 5;
    const std::int32_t x = static_cast<std::int32_t>(noise_state % width);
    const std::int32_t y = static_cast<std::int32_t>((noise_state >> 8) % height);
    if (!object.contains(x, y)) {
      quick_select_fill_rect(image, width, patchy::Rect{x, y, 1, 1}, {90, 90, 90, 255});
    }
  }

  // The stroke covers the object's middle; the remaining margin to the object edges stays
  // within the spread budget (growth is bounded like Photoshop's, so a tiny stroke in a huge
  // shape deliberately does NOT grab far corners).
  std::vector<std::uint8_t> seeds(static_cast<std::size_t>(width) * height, 0);
  const patchy::Rect seed_rect{70, 50, 60, 50};
  quick_select_fill_mask(seeds, width, seed_rect, 255);

  patchy::QuickSelectParams params;
  params.brush_radius = 10;
  const auto result = patchy::quick_select_segment(image.data(), width, height,
                                                   static_cast<std::ptrdiff_t>(width) * 4, nullptr,
                                                   seeds.data(), seed_rect, params);
  CHECK(!result.empty());

  std::vector<std::uint8_t> selection(static_cast<std::size_t>(width) * height, 0);
  quick_select_apply_delta(selection, width, result, false);
  const int object_hits = quick_select_count_in_rect(selection, width, object);
  const int total_hits = quick_select_count_in_rect(selection, width, patchy::Rect::from_size(width, height));
  const int object_area = object.width * object.height;
  const int background_area = width * height - object_area;
  CHECK(object_hits >= object_area * 95 / 100);
  CHECK(total_hits - object_hits <= background_area / 100);
}

void quick_select_second_stroke_adds_monotonically() {
  const std::int32_t width = 200;
  const std::int32_t height = 150;
  const patchy::Rect object{60, 40, 80, 70};
  auto image = quick_select_image(width, height, {255, 255, 255, 255});
  quick_select_fill_rect(image, width, object, {40, 90, 200, 255});

  std::vector<std::uint8_t> selection(static_cast<std::size_t>(width) * height, 0);
  patchy::QuickSelectParams params;
  params.brush_radius = 8;

  std::vector<std::uint8_t> seeds(static_cast<std::size_t>(width) * height, 0);
  const patchy::Rect first_seed{65, 45, 25, 60};
  quick_select_fill_mask(seeds, width, first_seed, 255);
  const auto first = patchy::quick_select_segment(image.data(), width, height,
                                                  static_cast<std::ptrdiff_t>(width) * 4, selection.data(),
                                                  seeds.data(), first_seed, params);
  CHECK(!first.empty());
  quick_select_apply_delta(selection, width, first, false);
  const int after_first = quick_select_count_in_rect(selection, width, object);

  std::fill(seeds.begin(), seeds.end(), std::uint8_t{0});
  const patchy::Rect second_seed{110, 45, 25, 60};
  quick_select_fill_mask(seeds, width, second_seed, 255);
  const auto second = patchy::quick_select_segment(image.data(), width, height,
                                                   static_cast<std::ptrdiff_t>(width) * 4, selection.data(),
                                                   seeds.data(), second_seed, params);
  // The second stroke may only add pixels that were not already selected.
  for (const auto& run : second.delta_runs) {
    for (std::int32_t x = run.x0; x <= run.x1; ++x) {
      CHECK(selection[static_cast<std::size_t>(run.y) * width + x] == 0);
    }
  }
  quick_select_apply_delta(selection, width, second, false);
  const int after_second = quick_select_count_in_rect(selection, width, object);
  CHECK(after_second >= after_first);
  CHECK(after_second >= object.width * object.height * 95 / 100);
  const int total = quick_select_count_in_rect(selection, width, patchy::Rect::from_size(width, height));
  CHECK(total - after_second <= (width * height - object.width * object.height) / 100);
}

void quick_select_subtract_removes_only_connected_area() {
  const std::int32_t width = 220;
  const std::int32_t height = 150;
  const patchy::Rect blob_a{20, 20, 80, 80};   // larger than a tap's spread budget
  const patchy::Rect blob_b{150, 80, 40, 40};
  auto image = quick_select_image(width, height, {255, 255, 255, 255});
  quick_select_fill_rect(image, width, blob_a, {30, 60, 180, 255});
  quick_select_fill_rect(image, width, blob_b, {30, 60, 180, 255});

  std::vector<std::uint8_t> selection(static_cast<std::size_t>(width) * height, 0);
  quick_select_fill_mask(selection, width, blob_a, 255);
  quick_select_fill_mask(selection, width, blob_b, 255);

  // A subtract tap in a blob much larger than the spread budget shaves a budget-sized area
  // around the brush, always within the previously-selected blob (Photoshop-like locality).
  std::vector<std::uint8_t> seeds(static_cast<std::size_t>(width) * height, 0);
  const patchy::Rect tap_rect{55, 55, 10, 10};
  quick_select_fill_mask(seeds, width, tap_rect, 255);
  patchy::QuickSelectParams params;
  params.brush_radius = 8;
  params.subtract = true;
  const auto tap = patchy::quick_select_segment(image.data(), width, height,
                                                static_cast<std::ptrdiff_t>(width) * 4, selection.data(),
                                                seeds.data(), tap_rect, params);
  CHECK(!tap.empty());
  for (const auto& run : tap.delta_runs) {
    CHECK(run.y >= blob_a.y && run.y < blob_a.y + blob_a.height);
    CHECK(run.x0 >= blob_a.x && run.x1 < blob_a.x + blob_a.width);
  }
  quick_select_apply_delta(selection, width, tap, true);
  const int after_tap = quick_select_count_in_rect(selection, width, blob_a);
  CHECK(after_tap < blob_a.width * blob_a.height);          // something was shaved...
  CHECK(after_tap >= blob_a.width * blob_a.height * 30 / 100);  // ...but a tap must not nuke the blob
  CHECK(quick_select_count_in_rect(selection, width, blob_b) == blob_b.width * blob_b.height);

  // A stroke covering most of the blob removes it entirely (the unbrushed remainder is within
  // the budget and cheaper to drop than to fence off), still without touching blob B.
  quick_select_fill_mask(selection, width, blob_a, 255);  // restore blob A
  std::fill(seeds.begin(), seeds.end(), std::uint8_t{0});
  const patchy::Rect stroke_rect{30, 30, 60, 60};
  quick_select_fill_mask(seeds, width, stroke_rect, 255);
  const auto stroke = patchy::quick_select_segment(image.data(), width, height,
                                                   static_cast<std::ptrdiff_t>(width) * 4, selection.data(),
                                                   seeds.data(), stroke_rect, params);
  CHECK(!stroke.empty());
  quick_select_apply_delta(selection, width, stroke, true);
  CHECK(quick_select_count_in_rect(selection, width, blob_a) <= blob_a.width * blob_a.height * 5 / 100);
  CHECK(quick_select_count_in_rect(selection, width, blob_b) == blob_b.width * blob_b.height);
}

// Regression for the July 2026 "brushing an eye selects half the face" report: on textured
// photo-like content whose stroke colors also appear in the background samples, the selection
// must hug the brushed object instead of flooding to the solve-window border. (The original
// cause was over-smoothed histograms plus a background-decontamination hack that turned every
// stroke color into strong foreground.)
void quick_select_photo_texture_stroke_stays_local() {
  const std::int32_t width = 260;
  const std::int32_t height = 200;
  std::vector<std::uint8_t> image(static_cast<std::size_t>(width) * height * 4);
  std::uint32_t noise_state = 0x1234ABCDu;
  const auto next_noise = [&noise_state] {
    noise_state ^= noise_state << 13;
    noise_state ^= noise_state >> 17;
    noise_state ^= noise_state << 5;
    return noise_state;
  };
  const double center_x = 130.0;
  const double center_y = 100.0;
  const double radius_x = 48.0;
  const double radius_y = 32.0;
  const auto ellipse_distance = [&](double x, double y) {
    const double dx = (x - center_x) / radius_x;
    const double dy = (y - center_y) / radius_y;
    return std::sqrt(dx * dx + dy * dy);
  };
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      // Skin-tone background with a horizontal gradient and per-pixel luminance noise.
      const int gradient = x * 30 / width;
      const int jitter = static_cast<int>(next_noise() % 25) - 12;
      const int skin_r = 215 + gradient / 2 + jitter;
      const int skin_g = 170 + gradient / 3 + jitter;
      const int skin_b = 140 + gradient / 3 + jitter;
      // Dark "eye" ellipse with a soft blended rim (a few pixels wide).
      const int dark_r = 70 + jitter / 2;
      const int dark_g = 45 + jitter / 2;
      const int dark_b = 35 + jitter / 2;
      const double edge = std::clamp((ellipse_distance(x, y) - 1.0) * (radius_x / 4.0), 0.0, 1.0);
      auto* px = image.data() + (static_cast<std::size_t>(y) * width + x) * 4;
      px[0] = static_cast<std::uint8_t>(std::clamp(
          static_cast<int>(std::lround(dark_r + (skin_r - dark_r) * edge)), 0, 255));
      px[1] = static_cast<std::uint8_t>(std::clamp(
          static_cast<int>(std::lround(dark_g + (skin_g - dark_g) * edge)), 0, 255));
      px[2] = static_cast<std::uint8_t>(std::clamp(
          static_cast<int>(std::lround(dark_b + (skin_b - dark_b) * edge)), 0, 255));
      px[3] = 255;
    }
  }

  std::vector<std::uint8_t> seeds(static_cast<std::size_t>(width) * height, 0);
  const patchy::Rect seed_rect{115, 92, 30, 14};  // well inside the ellipse
  quick_select_fill_mask(seeds, width, seed_rect, 255);

  patchy::QuickSelectParams params;
  params.brush_radius = 10;
  const auto result = patchy::quick_select_segment(image.data(), width, height,
                                                   static_cast<std::ptrdiff_t>(width) * 4, nullptr,
                                                   seeds.data(), seed_rect, params);
  CHECK(!result.empty());

  std::vector<std::uint8_t> selection(static_cast<std::size_t>(width) * height, 0);
  quick_select_apply_delta(selection, width, result, false);
  int inside_area = 0;
  int inside_hits = 0;
  int outside_hits = 0;
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const double distance = ellipse_distance(x, y);
      const bool selected = selection[static_cast<std::size_t>(y) * width + x] != 0;
      if (distance <= 1.0) {
        ++inside_area;
        inside_hits += selected ? 1 : 0;
      } else if (distance > 1.15 && selected) {
        ++outside_hits;  // beyond the soft rim: this is leakage
      }
    }
  }
  CHECK(inside_hits >= inside_area * 70 / 100);
  CHECK(outside_hits <= width * height * 3 / 100);
}

void quick_select_enhance_edge_smooths_staircase() {
  const std::int32_t width = 60;
  const std::int32_t height = 60;
  std::vector<std::uint8_t> mask(static_cast<std::size_t>(width) * height, 0);
  quick_select_fill_mask(mask, width, patchy::Rect{0, 0, 30, height}, 255);  // straight edge at x=30
  mask[static_cast<std::size_t>(10) * width + 31] = 255;  // 1px spur on the edge
  mask[static_cast<std::size_t>(15) * width + 15] = 0;    // 1px hole inside
  mask[static_cast<std::size_t>(45) * width + 45] = 255;  // isolated island outside

  auto banded = mask;  // band-restricted copy: only the top half may change
  patchy::enhance_selection_edge(mask, width, height, patchy::Rect::from_size(width, height));
  CHECK(mask[static_cast<std::size_t>(10) * width + 31] == 0);   // spur removed
  CHECK(mask[static_cast<std::size_t>(15) * width + 15] == 255); // hole filled
  CHECK(mask[static_cast<std::size_t>(45) * width + 45] == 0);   // island removed
  for (std::int32_t y = 0; y < height; ++y) {                    // straight edge intact
    CHECK(mask[static_cast<std::size_t>(y) * width + 29] == 255);
    CHECK(mask[static_cast<std::size_t>(y) * width + 30] == 0);
  }

  patchy::enhance_selection_edge(banded, width, height, patchy::Rect{0, 0, width, 30});
  CHECK(banded[static_cast<std::size_t>(10) * width + 31] == 0);   // inside band: smoothed
  CHECK(banded[static_cast<std::size_t>(45) * width + 45] == 255); // outside band: untouched
}

std::vector<std::uint8_t> magnetic_two_tone_image(std::int32_t width, std::int32_t height,
                                                  const std::function<bool(std::int32_t, std::int32_t)>& is_light,
                                                  std::uint8_t dark, std::uint8_t light) {
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const auto value = is_light(x, y) ? light : dark;
      auto* px = rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4U;
      px[0] = value;
      px[1] = value;
      px[2] = value;
      px[3] = 255;
    }
  }
  return rgba;
}

std::vector<patchy::PointI32> magnetic_reference_line(patchy::PointI32 from, patchy::PointI32 to) {
  std::vector<patchy::PointI32> line;
  const auto dx = std::abs(to.x - from.x);
  const auto dy = std::abs(to.y - from.y);
  const std::int32_t sx = from.x < to.x ? 1 : -1;
  const std::int32_t sy = from.y < to.y ? 1 : -1;
  std::int32_t error = dx - dy;
  auto point = from;
  while (true) {
    line.push_back(point);
    if (point == to) {
      break;
    }
    const auto doubled = 2 * error;
    if (doubled > -dy) {
      error -= dy;
      point.x += sx;
    }
    if (doubled < dx) {
      error += dx;
      point.y += sy;
    }
  }
  return line;
}

void check_magnetic_path_is_8_connected(const std::vector<patchy::PointI32>& path) {
  CHECK(!path.empty());
  for (std::size_t i = 1; i < path.size(); ++i) {
    CHECK(std::abs(path[i].x - path[i - 1].x) <= 1);
    CHECK(std::abs(path[i].y - path[i - 1].y) <= 1);
    CHECK(!(path[i] == path[i - 1]));
  }
}

void magnetic_lasso_path_snaps_to_synthetic_edge() {
  constexpr std::int32_t size = 128;
  // Triangle-wave boundary, amplitude +-4, slope +-1 per row (integer, deterministic).
  const auto boundary = [](std::int32_t y) { return 64 + std::abs(y % 16 - 8) - 4; };
  const auto rgba =
      magnetic_two_tone_image(size, size, [&](std::int32_t x, std::int32_t y) { return x >= boundary(y); }, 30, 220);
  patchy::LiveWireEngine engine;
  engine.set_image(rgba.data(), size, size, size * 4);
  engine.set_params({});
  const auto anchor = engine.snap({boundary(8) - 3, 8});
  CHECK(std::abs(anchor.x - boundary(anchor.y)) <= 2);
  CHECK(std::abs(anchor.y - 8) <= 5);
  engine.set_anchor(anchor);
  const auto target = engine.snap({boundary(120) + 3, 120});
  const auto path = engine.path_to(target);
  CHECK(path.size() >= 100);
  CHECK(path.front() == anchor);
  CHECK(path.back() == target);
  check_magnetic_path_is_8_connected(path);
  for (const auto& p : path) {
    CHECK(std::abs(p.x - boundary(p.y)) <= 2);
  }
}

void magnetic_lasso_flat_region_yields_straight_line() {
  constexpr std::int32_t size = 128;
  const auto rgba =
      magnetic_two_tone_image(size, size, [](std::int32_t, std::int32_t) { return false; }, 128, 128);
  patchy::LiveWireEngine engine;
  engine.set_image(rgba.data(), size, size, size * 4);
  engine.set_params({});
  CHECK((engine.snap({50, 50}) == patchy::PointI32{50, 50}));
  engine.set_anchor({10, 10});
  const auto path = engine.path_to({100, 60});
  const auto expected = magnetic_reference_line({10, 10}, {100, 60});
  CHECK(path.size() == expected.size());
  for (std::size_t i = 0; i < path.size(); ++i) {
    CHECK(path[i] == expected[i]);
  }
}

void magnetic_lasso_edge_contrast_gates_weak_edges() {
  constexpr std::int32_t size = 128;
  // Faint step: 128 -> 148 at x = 64 gives gradient G8 = 20.
  const auto rgba =
      magnetic_two_tone_image(size, size, [](std::int32_t x, std::int32_t) { return x >= 64; }, 128, 148);
  patchy::LiveWireEngine engine;
  engine.set_image(rgba.data(), size, size, size * 4);

  patchy::MagneticLassoParams low{};
  low.edge_contrast = 5;  // threshold 12 < 20: the faint edge attracts
  engine.set_params(low);
  engine.set_anchor(engine.snap({61, 10}));
  auto path = engine.path_to(engine.snap({61, 118}));
  check_magnetic_path_is_8_connected(path);
  bool rode_the_edge = false;
  for (const auto& p : path) {
    if (p.y > 20 && p.y < 100) {
      rode_the_edge = rode_the_edge || (p.x >= 62 && p.x <= 65);
    }
  }
  CHECK(rode_the_edge);

  patchy::MagneticLassoParams high{};
  high.edge_contrast = 60;  // threshold 153 > 20: gated, traces the literal line
  engine.set_params(high);
  CHECK((engine.snap({61, 10}) == patchy::PointI32{61, 10}));
  engine.set_anchor({61, 10});
  path = engine.path_to({61, 118});
  CHECK(path.size() == 109);
  for (const auto& p : path) {
    CHECK(p.x == 61);
  }
}

void magnetic_lasso_snap_respects_width() {
  constexpr std::int32_t size = 128;
  const auto rgba =
      magnetic_two_tone_image(size, size, [](std::int32_t x, std::int32_t) { return x >= 64; }, 30, 220);
  patchy::LiveWireEngine engine;
  engine.set_image(rgba.data(), size, size, size * 4);

  patchy::MagneticLassoParams wide{};
  wide.width = 10;  // radius 5 reaches the edge columns 63/64 from x = 61
  engine.set_params(wide);
  CHECK((engine.snap({61, 40}) == patchy::PointI32{63, 40}));

  patchy::MagneticLassoParams narrow{};
  narrow.width = 4;  // radius 2 cannot reach the edge from x = 58
  engine.set_params(narrow);
  CHECK((engine.snap({58, 40}) == patchy::PointI32{58, 40}));
}

void magnetic_lasso_is_deterministic() {
  constexpr std::int32_t width = 512;
  constexpr std::int32_t height = 64;
  const auto rgba = magnetic_two_tone_image(
      width, height, [](std::int32_t, std::int32_t y) { return y >= 32; }, 30, 220);
  const auto trace = [&] {
    patchy::LiveWireEngine engine;
    engine.set_image(rgba.data(), width, height, width * 4);
    engine.set_params({});
    engine.set_anchor(engine.snap({8, 30}));
    // A near target builds the anchor-centered field, then a far target forces the window
    // regrowth path; both legs must be reproducible.
    auto path = engine.path_to(engine.snap({120, 30}));
    auto far_path = engine.path_to(engine.snap({500, 30}));
    path.insert(path.end(), far_path.begin(), far_path.end());
    return path;
  };
  const auto first = trace();
  const auto second = trace();
  CHECK(first.size() == second.size());
  for (std::size_t i = 0; i < first.size(); ++i) {
    CHECK(first[i] == second[i]);
  }
  CHECK(first.size() > 500);
  for (const auto& p : first) {
    CHECK(p.y >= 29 && p.y <= 34);
  }
}

void magnetic_lasso_prefers_opaque_side_of_alpha_edge() {
  // Black art on transparent ground: the gradient lives in the alpha channel and spans an
  // anti-aliased ramp several pixels wide (columns 62..64 here). The wire and the snap must
  // settle on the opaque side of the ramp like Photoshop, not the translucent fringe.
  constexpr std::int32_t size = 128;
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size) * size * 4U, 0);
  const auto alpha_for_column = [](std::int32_t x) -> std::uint8_t {
    if (x < 62) {
      return 0;
    }
    switch (x) {
      case 62:
        return 64;
      case 63:
        return 160;
      case 64:
        return 240;
      default:
        return 255;
    }
  };
  for (std::int32_t y = 0; y < size; ++y) {
    for (std::int32_t x = 0; x < size; ++x) {
      rgba[(static_cast<std::size_t>(y) * size + x) * 4U + 3U] = alpha_for_column(x);
    }
  }
  patchy::LiveWireEngine engine;
  engine.set_image(rgba.data(), size, size, size * 4);
  engine.set_params({});
  const auto anchor = engine.snap({60, 20});
  CHECK(anchor.x == 64);
  CHECK(anchor.y == 20);
  engine.set_anchor(anchor);
  const auto path = engine.path_to(engine.snap({60, 110}));
  check_magnetic_path_is_8_connected(path);
  CHECK(path.size() >= 80);
  for (const auto& p : path) {
    CHECK(p.x >= 64);
    CHECK(p.x <= 66);
  }
}

void magnetic_lasso_node_budget_falls_back_to_straight_line() {
  constexpr std::int32_t size = 256;
  const auto rgba =
      magnetic_two_tone_image(size, size, [](std::int32_t x, std::int32_t) { return x >= 128; }, 30, 220);
  patchy::LiveWireEngine engine;
  engine.set_image(rgba.data(), size, size, size * 4);
  patchy::MagneticLassoParams tiny_budget{};
  tiny_budget.node_budget = 1;  // clamps to the 1024 floor, far below any usable window
  engine.set_params(tiny_budget);
  engine.set_anchor({10, 128});
  const auto path = engine.path_to({245, 128});
  const auto expected = magnetic_reference_line({10, 128}, {245, 128});
  CHECK(path.size() == expected.size());
  for (std::size_t i = 0; i < path.size(); ++i) {
    CHECK(path[i] == expected[i]);
  }
}

}  // namespace

int main() {
  patchy::test::suppress_crash_dialogs();
  const std::vector<TestCase> tests = {
      {"pixel_buffer_tracks_shape_and_rows", pixel_buffer_tracks_shape_and_rows},
      {"pixel_buffer_copy_shares_storage_until_mutated", pixel_buffer_copy_shares_storage_until_mutated},
      {"document_snapshot_shares_pixels_when_only_moving_a_layer",
       document_snapshot_shares_pixels_when_only_moving_a_layer},
      {"document_adds_and_finds_layers", document_adds_and_finds_layers},
      {"document_removes_layers_and_updates_active_layer", document_removes_layers_and_updates_active_layer},
      {"document_can_clear_active_layer", document_can_clear_active_layer},
      {"default_non_group_layer_id_selects_topmost_visible_unlocked_pixel_child",
       default_non_group_layer_id_selects_topmost_visible_unlocked_pixel_child},
      {"default_non_group_layer_id_uses_visible_adjustment_before_hidden_pixels",
       default_non_group_layer_id_uses_visible_adjustment_before_hidden_pixels},
      {"default_non_group_layer_id_ignores_locked_content_and_folder_only_trees",
       default_non_group_layer_id_ignores_locked_content_and_folder_only_trees},
      {"default_non_group_layer_id_allows_position_locked_pixels",
       default_non_group_layer_id_allows_position_locked_pixels},
      {"layer_drop_request_moves_multiple_layers_into_folder", layer_drop_request_moves_multiple_layers_into_folder},
      {"layer_drop_roots_ignore_selected_descendants", layer_drop_roots_ignore_selected_descendants},
      {"document_print_settings_default_and_copy", document_print_settings_default_and_copy},
      {"document_grid_guides_default_and_copy", document_grid_guides_default_and_copy},
      {"compositor_flattens_visible_layers", compositor_flattens_visible_layers},
      {"compositor_multiply_uses_empty_backdrop_as_transparent",
       compositor_multiply_uses_empty_backdrop_as_transparent},
      {"compositor_applies_extended_blend_modes", compositor_applies_extended_blend_modes},
      {"compositor_renders_layer_style_drop_shadow_gradient_and_stroke",
       compositor_renders_layer_style_drop_shadow_gradient_and_stroke},
      {"compositor_renders_layer_style_bevel_emboss", compositor_renders_layer_style_bevel_emboss},
      {"compositor_renders_bevel_across_thin_checkbox_edges",
       compositor_renders_bevel_across_thin_checkbox_edges},
      {"compositor_renders_layer_style_outer_glow", compositor_renders_layer_style_outer_glow},
      {"compositor_outer_glow_spread_stays_within_size",
       compositor_outer_glow_spread_stays_within_size},
      {"compositor_outer_glow_spread_uses_circular_distance_field",
       compositor_outer_glow_spread_uses_circular_distance_field},
      {"compositor_drop_shadow_preserves_source_alpha",
       compositor_drop_shadow_preserves_source_alpha},
      {"compositor_drop_shadow_preserves_connected_antialias_alpha",
       compositor_drop_shadow_preserves_connected_antialias_alpha},
      {"compositor_drop_shadow_soft_mask_has_smooth_falloff",
       compositor_drop_shadow_soft_mask_has_smooth_falloff},
      {"compositor_outer_glow_preserves_source_alpha",
       compositor_outer_glow_preserves_source_alpha},
      {"compositor_outer_glow_antialias_strength_does_not_create_streaks",
       compositor_outer_glow_antialias_strength_does_not_create_streaks},
      {"compositor_large_text_style_spread_keeps_rounded_silhouette",
       compositor_large_text_style_spread_keeps_rounded_silhouette},
      {"layer_style_spread_dilation_keeps_circular_radius",
       layer_style_spread_dilation_keeps_circular_radius},
      {"visible_alpha_bounds_track_sparse_rgba_pixels", visible_alpha_bounds_track_sparse_rgba_pixels},
      {"layer_style_preview_marks_large_or_broad_effects_expensive",
       layer_style_preview_marks_large_or_broad_effects_expensive},
      {"compositor_renders_sparse_outer_glow_from_visible_alpha_bounds",
       compositor_renders_sparse_outer_glow_from_visible_alpha_bounds},
      {"compositor_renders_sparse_drop_shadow_from_visible_alpha_bounds",
       compositor_renders_sparse_drop_shadow_from_visible_alpha_bounds},
      {"compositor_renders_layer_style_color_overlay", compositor_renders_layer_style_color_overlay},
      {"compositor_renders_drop_shadow_spread", compositor_renders_drop_shadow_spread},
      {"compositor_drop_shadow_full_spread_keeps_rounded_support",
       compositor_drop_shadow_full_spread_keeps_rounded_support},
      {"compositor_renders_drop_shadow_beyond_outer_glow",
       compositor_renders_drop_shadow_beyond_outer_glow},
      {"compositor_renders_inner_shadow", compositor_renders_inner_shadow},
      {"compositor_renders_inner_glow", compositor_renders_inner_glow},
      {"compositor_inner_shadow_full_choke_keeps_rounded_interior",
       compositor_inner_shadow_full_choke_keeps_rounded_interior},
      {"compositor_inner_glow_full_choke_keeps_rounded_interior",
       compositor_inner_glow_full_choke_keeps_rounded_interior},
      {"compositor_inner_glow_center_choke_erodes_matte_geometrically",
       compositor_inner_glow_center_choke_erodes_matte_geometrically},
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
      {"psd_grid_guides_resource_round_trip_and_replaces_duplicates",
       psd_grid_guides_resource_round_trip_and_replaces_duplicates},
      {"psd_layered_rgb8_round_trips_pixel_layers", psd_layered_rgb8_round_trips_pixel_layers},
      {"psd_layered_writer_uses_rle_for_compressible_layer_channels",
       psd_layered_writer_uses_rle_for_compressible_layer_channels},
      {"psd_layer_locks_import_and_export_lspf", psd_layer_locks_import_and_export_lspf},
      {"psd_layer_masks_render_and_round_trip", psd_layer_masks_render_and_round_trip},
      {"psd_layer_mask_link_state_round_trips", psd_layer_mask_link_state_round_trips},
      {"psd_layer_record_flags_mark_photoshop5_layers", psd_layer_record_flags_mark_photoshop5_layers},
      {"layer_mask_shapes_effects_regardless_of_link", layer_mask_shapes_effects_regardless_of_link},
      {"psd_layer_mask_hides_effects_round_trip", psd_layer_mask_hides_effects_round_trip},
      {"layer_mask_hides_effects_clips_exterior_effect_output",
       layer_mask_hides_effects_clips_exterior_effect_output},
      {"psd_photoshop_mask_hides_effects_fixture_clips_shadow",
       psd_photoshop_mask_hides_effects_fixture_clips_shadow},
      {"layer_stroke_outlines_semi_transparent_regions_without_fill",
       layer_stroke_outlines_semi_transparent_regions_without_fill},
      {"exact_squared_distance_transform_matches_bruteforce",
       exact_squared_distance_transform_matches_bruteforce},
      {"layer_stroke_center_band_width_matches_size", layer_stroke_center_band_width_matches_size},
      {"layer_stroke_center_has_no_seam_on_antialiased_contour",
       layer_stroke_center_has_no_seam_on_antialiased_contour},
      {"layer_stroke_outside_antialiases_corners", layer_stroke_outside_antialiases_corners},
      {"layer_stroke_fractional_size_renders_partial_ring",
       layer_stroke_fractional_size_renders_partial_ring},
      {"psd_photoshop_stroke_partial_alpha_fixture_matches",
       psd_photoshop_stroke_partial_alpha_fixture_matches},
      {"psd_photoshop_stroke_positions_fixture_matches",
       psd_photoshop_stroke_positions_fixture_matches},
      {"psd_global_link_blocks_round_trip_with_smart_object_layers",
       psd_global_link_blocks_round_trip_with_smart_object_layers},
      {"psd_dangling_smart_object_blocks_are_stripped", psd_dangling_smart_object_blocks_are_stripped},
      {"psd_smart_object_sources_survive_resave_if_available",
       psd_smart_object_sources_survive_resave_if_available},
      {"psb_layered_round_trip_preserves_layers_and_blocks",
       psb_layered_round_trip_preserves_layers_and_blocks},
      {"psb_flat_round_trip_reads_composite", psb_flat_round_trip_reads_composite},
      {"psb_photoshop_fixture_round_trips", psb_photoshop_fixture_round_trips},
      {"psd_smart_object_fixture_parses_placement_and_source",
       psd_smart_object_fixture_parses_placement_and_source},
      {"psd_smart_object_clean_resave_preserves_blocks_byte_identically",
       psd_smart_object_clean_resave_preserves_blocks_byte_identically},
      {"psd_smart_object_move_regenerates_placed_blocks", psd_smart_object_move_regenerates_placed_blocks},
      {"smart_object_rescaled_placement_matches_photoshop_replace_rule",
       smart_object_rescaled_placement_matches_photoshop_replace_rule},
      {"smart_object_store_remove_and_generated_uuid_shape",
       smart_object_store_remove_and_generated_uuid_shape},
      {"psd_smart_object_committed_psb_contents_round_trip",
       psd_smart_object_committed_psb_contents_round_trip},
      {"psd_descriptor_writer_round_trips_sold", psd_descriptor_writer_round_trips_sold},
      {"psd_descriptor_writer_round_trips_smart_filter_sold_if_available",
       psd_descriptor_writer_round_trips_smart_filter_sold_if_available},
      {"psb_write_accepts_over_30k_dimension_psd_rejects",
       psb_write_accepts_over_30k_dimension_psd_rejects},
      {"psd_layered_writer_bytes_are_stable", psd_layered_writer_bytes_are_stable},
      {"psd_photoshop_unlinked_mask_fixture_reads_unlinked",
       psd_photoshop_unlinked_mask_fixture_reads_unlinked},
      {"psd_generated_drop_shadow_marks_angle_as_local",
       psd_generated_drop_shadow_marks_angle_as_local},
      {"psd_drop_shadow_resolves_photoshop_global_light",
       psd_drop_shadow_resolves_photoshop_global_light},
      {"psd_photoshop_global_light_shadow_fixture_resolves_angle",
       psd_photoshop_global_light_shadow_fixture_resolves_angle},
      {"psd_arrows_load_save_stays_compressed_if_available",
       psd_arrows_load_save_stays_compressed_if_available},
      {"psd_layer_styles_round_trip_patchy_effects", psd_layer_styles_round_trip_patchy_effects},
      {"psd_writer_uses_preserved_photoshop_style_blocks_without_private_duplicates",
       psd_writer_uses_preserved_photoshop_style_blocks_without_private_duplicates},
      {"psd_arrows_imports_photoshop_inner_effects",
       psd_arrows_imports_photoshop_inner_effects},
      {"psd_qual_rca_pinout_imports_white_drop_shadows",
       psd_qual_rca_pinout_imports_white_drop_shadows},
      {"psd_qual_rca_pinout_point_text_imports_as_point_text",
       psd_qual_rca_pinout_point_text_imports_as_point_text},
      {"psd_qual_rca_pinout_round_trips_styles_and_text_metadata",
       psd_qual_rca_pinout_round_trips_styles_and_text_metadata},
      {"psd_qual_rca_pinout_writes_comparison_artifacts",
       psd_qual_rca_pinout_writes_comparison_artifacts},
      {"psd_checkbox_bevel_emboss_writes_comparison_artifacts_if_available",
       psd_checkbox_bevel_emboss_writes_comparison_artifacts_if_available},
      {"psd_adjustment_layers_render_and_round_trip", psd_adjustment_layers_render_and_round_trip},
      {"psd_levels_adjustment_channel_round_trips", psd_levels_adjustment_channel_round_trips},
      {"psd_levels_adjustment_writes_native_levl_and_patchy_fallback",
       psd_levels_adjustment_writes_native_levl_and_patchy_fallback},
      {"psd_native_levels_adjustment_imports_without_patchy_block",
       psd_native_levels_adjustment_imports_without_patchy_block},
      {"psd_native_levels_overrides_stale_patchy_fallback",
       psd_native_levels_overrides_stale_patchy_fallback},
      {"psd_writer_uses_photoshop_bottom_to_top_layer_record_order",
       psd_writer_uses_photoshop_bottom_to_top_layer_record_order},
      {"psd_reader_tolerates_legacy_patchy_top_to_bottom_background_files",
       psd_reader_tolerates_legacy_patchy_top_to_bottom_background_files},
      {"psd_reader_preserves_layer_group_hierarchy", psd_reader_preserves_layer_group_hierarchy},
      {"psd_writer_round_trips_layer_groups", psd_writer_round_trips_layer_groups},
      {"psd_ipad_main_v04_preserves_folders_if_available", psd_ipad_main_v04_preserves_folders_if_available},
      {"psd_writer_preserves_layer_additional_blocks_and_long_names",
       psd_writer_preserves_layer_additional_blocks_and_long_names},
      {"psd_import_regenerates_large_styled_text_preview_alpha",
       psd_import_regenerates_large_styled_text_preview_alpha},
      {"psd_writer_exports_patchy_rich_text_as_photoshop_type",
       psd_writer_exports_patchy_rich_text_as_photoshop_type},
      {"psd_writer_preserves_imported_photoshop_text_geometry",
       psd_writer_preserves_imported_photoshop_text_geometry},
      {"psd_writer_prefers_patchy_text_transform_over_imported_geometry",
       psd_writer_prefers_patchy_text_transform_over_imported_geometry},
      {"psd_writer_exports_point_text_with_photoshop_baseline_origin",
       psd_writer_exports_point_text_with_photoshop_baseline_origin},
      {"psd_writer_maps_text_raster_bounds_into_transform_local_space",
       psd_writer_maps_text_raster_bounds_into_transform_local_space},
      {"psd_writer_ignores_stale_imported_geometry_for_patchy_owned_text_frame",
       psd_writer_ignores_stale_imported_geometry_for_patchy_owned_text_frame},
      {"psd_writer_regenerates_same_length_patchy_text_without_stale_template",
       psd_writer_regenerates_same_length_patchy_text_without_stale_template},
      {"psd_writer_ignores_stale_type_template_after_patchy_text_edit",
       psd_writer_ignores_stale_type_template_after_patchy_text_edit},
      {"psd_extended_blend_modes_round_trip", psd_extended_blend_modes_round_trip},
      {"psd_text_layer_engine_data_renders_placeholder_text",
       psd_text_layer_engine_data_renders_placeholder_text},
      {"psd_text_engine_data_normalizes_photoshop_line_breaks_and_font_style",
       psd_text_engine_data_normalizes_photoshop_line_breaks_and_font_style},
      {"psd_text_engine_data_preserves_paragraph_layout_runs",
       psd_text_engine_data_preserves_paragraph_layout_runs},
      {"psd_text_engine_data_humanizes_postscript_font_family_names",
       psd_text_engine_data_humanizes_postscript_font_family_names},
      {"psd_text_engine_data_resolves_hyphenated_font_family_names",
       psd_text_engine_data_resolves_hyphenated_font_family_names},
      {"psd_writer_emits_photoshop_text_line_breaks",
       psd_writer_emits_photoshop_text_line_breaks},
      {"psd_writer_emits_v2_paragraph_layout",
       psd_writer_emits_v2_paragraph_layout},
      {"psd_reader_regenerates_patchy_generated_type_blocks_after_reopen",
       psd_reader_regenerates_patchy_generated_type_blocks_after_reopen},
      {"psd_horror_virtualboy_imports_multiline_bold_text_if_available",
       psd_horror_virtualboy_imports_multiline_bold_text_if_available},
      {"psd_arduboy_real_file_renders_if_available", psd_arduboy_real_file_renders_if_available},
      {"psd_title_screen_demo_layer_styles_render_if_available",
       psd_title_screen_demo_layer_styles_render_if_available},
      {"psd_duke_nukem_mobile_text_style_renders_if_available",
       psd_duke_nukem_mobile_text_style_renders_if_available},
      {"layer_affine_transform_metadata_parses_serializes_and_composes",
       layer_affine_transform_metadata_parses_serializes_and_composes},
      {"layer_lock_flags_and_inheritance_work", layer_lock_flags_and_inheritance_work},
      {"moved_layer_metadata_translates_linked_masks_and_vector_paths",
       moved_layer_metadata_translates_linked_masks_and_vector_paths},
      {"moved_layer_metadata_leaves_unlinked_masks_stationary",
       moved_layer_metadata_leaves_unlinked_masks_stationary},
      {"layer_content_revision_ignores_translation_and_tracks_render_content",
       layer_content_revision_ignores_translation_and_tracks_render_content},
      {"tool_brush_draws_color_and_writes_artifact", tool_brush_draws_color_and_writes_artifact},
      {"tool_brush_opacity_and_bounded_layer_expansion_work",
       tool_brush_opacity_and_bounded_layer_expansion_work},
      {"tool_brush_softness_feathers_edge_alpha", tool_brush_softness_feathers_edge_alpha},
      {"tool_brush_repaints_translucent_pixels_without_color_halo",
       tool_brush_repaints_translucent_pixels_without_color_halo},
      {"brush_tip_mips_and_scaling_preserve_shape", brush_tip_mips_and_scaling_preserve_shape},
      {"tool_brush_tip_stamps_bitmap_mask", tool_brush_tip_stamps_bitmap_mask},
      {"tool_brush_tip_rotation_and_roundness_transform_stamp",
       tool_brush_tip_rotation_and_roundness_transform_stamp},
      {"tool_brush_tip_segment_spacing_carries_across_segments",
       tool_brush_tip_segment_spacing_carries_across_segments},
      {"brush_dynamics_rng_sequence_is_stable", brush_dynamics_rng_sequence_is_stable},
      {"brush_dynamics_activation_gates_fields", brush_dynamics_activation_gates_fields},
      {"brush_dynamics_control_values", brush_dynamics_control_values},
      {"tool_brush_tip_controls_at_full_value_change_nothing",
       tool_brush_tip_controls_at_full_value_change_nothing},
      {"tool_brush_tip_size_control_pressure_scales_dabs", tool_brush_tip_size_control_pressure_scales_dabs},
      {"tool_brush_tip_opacity_control_respects_minimum", tool_brush_tip_opacity_control_respects_minimum},
      {"tool_brush_tip_inactive_dynamics_change_nothing", tool_brush_tip_inactive_dynamics_change_nothing},
      {"tool_brush_tip_size_jitter_shrinks_dabs_deterministically",
       tool_brush_tip_size_jitter_shrinks_dabs_deterministically},
      {"tool_brush_tip_angle_direction_follows_stroke", tool_brush_tip_angle_direction_follows_stroke},
      {"tool_brush_tip_angle_fade_and_jitter_rotate_dabs", tool_brush_tip_angle_fade_and_jitter_rotate_dabs},
      {"tool_brush_tip_flip_jitter_mirrors_stamp", tool_brush_tip_flip_jitter_mirrors_stamp},
      {"tool_brush_tip_scatter_offsets_perpendicular_to_stroke",
       tool_brush_tip_scatter_offsets_perpendicular_to_stroke},
      {"tool_brush_tip_count_stamps_multiple_dabs_per_step", tool_brush_tip_count_stamps_multiple_dabs_per_step},
      {"tool_brush_tip_opacity_jitter_varies_dab_alpha", tool_brush_tip_opacity_jitter_varies_dab_alpha},
      {"tool_brush_tip_dynamics_carry_across_segments", tool_brush_tip_dynamics_carry_across_segments},
      {"tool_brush_tip_erases_and_respects_gates", tool_brush_tip_erases_and_respects_gates},
      {"brush_tip_softening_feathers_edges", brush_tip_softening_feathers_edges},
      {"tool_brush_tip_large_stamp_stroke_is_fast", tool_brush_tip_large_stamp_stroke_is_fast},
      {"abr_v6_fixture_parses_brushes_names_and_spacing", abr_v6_fixture_parses_brushes_names_and_spacing},
      {"abr_dynamics_fixture_extracts_shape_and_scatter", abr_dynamics_fixture_extracts_shape_and_scatter},
      {"abr_myer_brushes_have_default_dynamics", abr_myer_brushes_have_default_dynamics},
      {"abr_v6_desc_controls_import", abr_v6_desc_controls_import},
      {"abr_v1_parses_sampled_brush_and_skips_computed", abr_v1_parses_sampled_brush_and_skips_computed},
      {"abr_v2_parses_named_rle_and_16bit_brushes", abr_v2_parses_named_rle_and_16bit_brushes},
      {"abr_rejects_corrupt_truncated_and_empty_files", abr_rejects_corrupt_truncated_and_empty_files},
      {"tool_one_pixel_brush_segment_snaps_fractional_points_to_one_pixel",
       tool_one_pixel_brush_segment_snaps_fractional_points_to_one_pixel},
      {"tool_wide_brush_segment_is_fast_and_writes_artifact",
       tool_wide_brush_segment_is_fast_and_writes_artifact},
      {"tool_brush_segment_accepts_float_endpoints", tool_brush_segment_accepts_float_endpoints},
      {"tool_brush_roundness_and_angle_shape_dabs", tool_brush_roundness_and_angle_shape_dabs},
      {"tool_eraser_clears_alpha_and_writes_artifact", tool_eraser_clears_alpha_and_writes_artifact},
      {"tool_eraser_converts_rgb_layer_to_transparency", tool_eraser_converts_rgb_layer_to_transparency},
      {"tool_smudge_drags_source_pixels_and_writes_artifact", tool_smudge_drags_source_pixels_and_writes_artifact},
      {"tool_line_draws_and_writes_artifact", tool_line_draws_and_writes_artifact},
      {"tool_rectangle_draws_outline_and_writes_artifact", tool_rectangle_draws_outline_and_writes_artifact},
      {"tool_ellipse_draws_and_writes_artifact", tool_ellipse_draws_and_writes_artifact},
      {"tool_filled_ellipse_uses_direct_fill_and_writes_artifact",
       tool_filled_ellipse_uses_direct_fill_and_writes_artifact},
      {"tool_thick_ellipse_outline_avoids_buildup", tool_thick_ellipse_outline_avoids_buildup},
      {"tool_filled_ellipse_respects_softness", tool_filled_ellipse_respects_softness},
      {"tool_ellipse_outline_thickness_is_uniform", tool_ellipse_outline_thickness_is_uniform},
      {"tool_rounded_rectangle_rounds_corners", tool_rounded_rectangle_rounds_corners},
      {"tool_fill_rect_honors_opacity_and_softness_feather",
       tool_fill_rect_honors_opacity_and_softness_feather},
      {"tool_fill_bucket_fills_region_and_writes_artifact", tool_fill_bucket_fills_region_and_writes_artifact},
      {"tool_gradient_draws_foreground_to_background_and_writes_artifact",
       tool_gradient_draws_foreground_to_background_and_writes_artifact},
      {"tool_gradient_supports_custom_stops_radial_reverse_and_alpha",
       tool_gradient_supports_custom_stops_radial_reverse_and_alpha},
      {"tool_fill_selection_draws_only_selection_and_writes_artifact", tool_fill_selection_draws_only_selection_and_writes_artifact},
      {"tool_clear_selection_erases_only_selection_and_writes_artifact", tool_clear_selection_erases_only_selection_and_writes_artifact},
      {"tool_clear_transparent_selection_is_noop", tool_clear_transparent_selection_is_noop},
      {"tool_clear_selected_opaque_pixels_reports_exact_bounds",
       tool_clear_selected_opaque_pixels_reports_exact_bounds},
      {"tool_fill_clear_gradient_respect_complex_selection_mask",
       tool_fill_clear_gradient_respect_complex_selection_mask},
      {"tool_lock_transparent_pixels_preserves_alpha", tool_lock_transparent_pixels_preserves_alpha},
      {"tool_clear_rgb_selection_converts_only_when_pixels_change",
       tool_clear_rgb_selection_converts_only_when_pixels_change},
      {"tool_write_paths_digest_baseline", tool_write_paths_digest_baseline},
      {"palette_lut_snaps_within_quantization_bound_and_is_idempotent",
       palette_lut_snaps_within_quantization_bound_and_is_idempotent},
      {"palette_snap_pixel_thresholds_alpha_and_ignores_low_channel_buffers",
       palette_snap_pixel_thresholds_alpha_and_ignores_low_channel_buffers},
      {"palette_remap_exact_color_is_lossless_and_bounded", palette_remap_exact_color_is_lossless_and_bounded},
      {"palette_quantize_uses_exact_colors_then_median_cut", palette_quantize_uses_exact_colors_then_median_cut},
      {"palette_apply_dither_outputs_only_palette_colors_and_is_deterministic",
       palette_apply_dither_outputs_only_palette_colors_and_is_deterministic},
      {"palette_io_round_trips_all_writer_formats", palette_io_round_trips_all_writer_formats},
      {"palette_presets_are_well_formed", palette_presets_are_well_formed},
      {"tool_writes_snap_to_palette_when_constrained", tool_writes_snap_to_palette_when_constrained},
      {"bmp_palette_mode_doc_exports_exact_document_palette",
       bmp_palette_mode_doc_exports_exact_document_palette},
      {"psd_round_trips_palette_resource", psd_round_trips_palette_resource},
      {"document_palette_editing_copies_and_syncs_indexed_mirror",
       document_palette_editing_copies_and_syncs_indexed_mirror},
      {"tool_flip_horizontal_changes_pixels_and_writes_artifact", tool_flip_horizontal_changes_pixels_and_writes_artifact},
      {"tool_flip_vertical_changes_pixels_and_writes_artifact", tool_flip_vertical_changes_pixels_and_writes_artifact},
      {"document_crop_to_selection_changes_canvas_and_writes_artifact",
       document_crop_to_selection_changes_canvas_and_writes_artifact},
      {"document_canvas_resize_expands_layers_for_editing", document_canvas_resize_expands_layers_for_editing},
      {"document_canvas_resize_honors_anchor_and_extension_color",
       document_canvas_resize_honors_anchor_and_extension_color},
      {"document_image_resize_scales_layers_and_writes_artifact",
       document_image_resize_scales_layers_and_writes_artifact},
      {"document_rotate_clockwise_changes_canvas_and_writes_artifact",
       document_rotate_clockwise_changes_canvas_and_writes_artifact},
      {"document_rotate_counterclockwise_changes_canvas_and_writes_artifact",
       document_rotate_counterclockwise_changes_canvas_and_writes_artifact},
      {"tool_stroke_selection_draws_border_and_writes_artifact", tool_stroke_selection_draws_border_and_writes_artifact},
      {"layer_merge_visible_creates_flattened_artifact", layer_merge_visible_creates_flattened_artifact},
      {"filters_register_and_apply", filters_register_and_apply},
      {"filters_builtin_effects_apply_and_write_artifacts", filters_builtin_effects_apply_and_write_artifacts},
      {"bmp_reader_rejects_invalid_headers", bmp_reader_rejects_invalid_headers},
      {"bmp_indexed_reads_2_4_8_bit_palettes_and_rows", bmp_indexed_reads_2_4_8_bit_palettes_and_rows},
      {"bmp_exact_indexed_writes_and_round_trips", bmp_exact_indexed_writes_and_round_trips},
      {"bmp_exact_indexed_fails_when_palette_overflows", bmp_exact_indexed_fails_when_palette_overflows},
      {"bmp_quantized_indexed_writes_deterministically", bmp_quantized_indexed_writes_deterministically},
      {"bmp_palette_file_export_uses_pal_and_bmp_palettes", bmp_palette_file_export_uses_pal_and_bmp_palettes},
      {"bmp_rgb24_and_rgba32_write_and_read", bmp_rgb24_and_rgba32_write_and_read},
      {"string_utils_normalize_extensions_and_names", string_utils_normalize_extensions_and_names},
      {"format_registry_finds_psd", format_registry_finds_psd},
      {"format_registry_allows_read_only_handlers", format_registry_allows_read_only_handlers},
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
      {"plugin_host_and_legacy_probe_work", plugin_host_and_legacy_probe_work},
      {"tile_cache_stores_and_invalidates", tile_cache_stores_and_invalidates},
      {"color_manager_assigns_profiles", color_manager_assigns_profiles},
      {"quick_select_maxflow_solves_tiny_grid", quick_select_maxflow_solves_tiny_grid},
      {"quick_select_maxflow_matches_reference_on_random_grids",
       quick_select_maxflow_matches_reference_on_random_grids},
      {"quick_select_stroke_grabs_flat_region_and_respects_edges",
       quick_select_stroke_grabs_flat_region_and_respects_edges},
      {"quick_select_second_stroke_adds_monotonically", quick_select_second_stroke_adds_monotonically},
      {"quick_select_subtract_removes_only_connected_area", quick_select_subtract_removes_only_connected_area},
      {"quick_select_photo_texture_stroke_stays_local", quick_select_photo_texture_stroke_stays_local},
      {"quick_select_enhance_edge_smooths_staircase", quick_select_enhance_edge_smooths_staircase},
      {"magnetic_lasso_path_snaps_to_synthetic_edge", magnetic_lasso_path_snaps_to_synthetic_edge},
      {"magnetic_lasso_flat_region_yields_straight_line", magnetic_lasso_flat_region_yields_straight_line},
      {"magnetic_lasso_edge_contrast_gates_weak_edges", magnetic_lasso_edge_contrast_gates_weak_edges},
      {"magnetic_lasso_snap_respects_width", magnetic_lasso_snap_respects_width},
      {"magnetic_lasso_is_deterministic", magnetic_lasso_is_deterministic},
      {"magnetic_lasso_prefers_opaque_side_of_alpha_edge",
       magnetic_lasso_prefers_opaque_side_of_alpha_edge},
      {"magnetic_lasso_node_budget_falls_back_to_straight_line",
       magnetic_lasso_node_budget_falls_back_to_straight_line},
  };

  int failures = 0;
  for (const auto& test : tests) {
    try {
      test.run();
      std::cout << "[PASS] " << test.name << '\n';
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
    }
  }

  return failures == 0 ? 0 : 1;
}
