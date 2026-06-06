#include "color/color_management.hpp"
#include "core/adjustment_layer.hpp"
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "filters/filter_registry.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/format_registry.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "plugins/plugin_host.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_document_io.hpp"
#include "core/pixel_tools.hpp"
#include "render/compositor.hpp"
#include "render/layer_compositor.hpp"
#include "render/tile_cache.hpp"
#include "support/string_utils.hpp"
#include "test_harness.hpp"

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

std::uint32_t read_u32_be_at(std::span<const std::uint8_t> bytes, std::size_t offset) {
  CHECK(offset + 4U <= bytes.size());
  return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
         static_cast<std::uint32_t>(bytes[offset + 3U]);
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

std::vector<std::uint8_t> utf16be_test_bytes(std::string_view text) {
  std::vector<std::uint8_t> bytes;
  for (const auto ch : text) {
    bytes.push_back(0);
    bytes.push_back(static_cast<std::uint8_t>(ch));
  }
  return bytes;
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
#ifdef PATCHY_SOURCE_DIR
  return std::filesystem::path(PATCHY_SOURCE_DIR) / "test-fixtures" / "psd" / "qual_rca_pinout.psd";
#else
  return std::filesystem::path("test-fixtures") / "psd" / "qual_rca_pinout.psd";
#endif
}

std::filesystem::path arrows_fixture_path() {
#ifdef PATCHY_SOURCE_DIR
  return std::filesystem::path(PATCHY_SOURCE_DIR) / "test-fixtures" / "psd" / "arrows.psd";
#else
  return std::filesystem::path("test-fixtures") / "psd" / "arrows.psd";
#endif
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
      {patchy::BlendMode::Saturation, 53, 120, 187},
      {patchy::BlendMode::Luminosity, 109, 130, 151},
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
  CHECK(psd_layer_block_payload(extra_data, "lfx2").has_value());
  CHECK(!psd_layer_block_payload(extra_data, "plFX").has_value());
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
  const auto path = std::filesystem::path("D:/projects/proton_svn/RTPeople/media/interface/checkbox.psd");
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
  const auto path = std::filesystem::path("D:/projects/proton/RTDink/media/interface/ipad/ipad_main_v04.psd");
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
  CHECK(std::abs(read_f64_be_at(*text_payload, 42U) - 51.0) < 0.000001);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 16U) == 12U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 12U) == 18U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 8U) == 92U);
  CHECK(read_u32_be_at(*text_payload, text_payload->size() - 4U) == 50U);
}

void psd_writer_updates_same_length_imported_text_from_original_type_template() {
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
  CHECK(read_u32_be_at(edited_payload, edited_payload.size() - 16U) == 0U);
  CHECK(read_u32_be_at(edited_payload, edited_payload.size() - 12U) == 0U);
  CHECK(read_u32_be_at(edited_payload, edited_payload.size() - 8U) == 0U);
  CHECK(read_u32_be_at(edited_payload, edited_payload.size() - 4U) == 0U);
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
  CHECK(payload_text.find("/RunLengthArray [ " + std::to_string(first_length) + ' ' +
                          std::to_string(second_length + 1) + " ]") != std::string::npos);

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2);
  const auto& paragraph_runs = read.layers().back().metadata().at(patchy::kLayerMetadataTextParagraphRuns);
  CHECK(paragraph_runs.find("v2\n0\t" + std::to_string(first_length) + "\tleft\t-24\t24\t0\t0\t24") == 0);
}

void psd_horror_virtualboy_imports_multiline_bold_text_if_available() {
  const auto path = std::filesystem::path("D:/projects/C2/MiscPrints/Horror VirtualBoy.psd");
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
  const auto path = std::filesystem::path("D:/projects/C2/MiscPrints/Arduboy.psd");
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
  const auto path = std::filesystem::path("D:/projects/DungeonScroll/media/Demo/Title Screen_demo.psd");
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
  const auto path = std::filesystem::path("C:/temp/Duke nukem mobile.psd");
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
  CHECK(probe.supported);
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

}  // namespace

int main() {
  patchy::test::suppress_crash_dialogs();
  const std::vector<TestCase> tests = {
      {"pixel_buffer_tracks_shape_and_rows", pixel_buffer_tracks_shape_and_rows},
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
      {"compositor_renders_drop_shadow_beyond_outer_glow",
       compositor_renders_drop_shadow_beyond_outer_glow},
      {"compositor_renders_inner_shadow", compositor_renders_inner_shadow},
      {"compositor_renders_inner_glow", compositor_renders_inner_glow},
      {"psd_flat_rgb8_round_trips", psd_flat_rgb8_round_trips},
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
      {"psd_layer_locks_import_and_export_lspf", psd_layer_locks_import_and_export_lspf},
      {"psd_layer_masks_render_and_round_trip", psd_layer_masks_render_and_round_trip},
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
      {"psd_writer_updates_same_length_imported_text_from_original_type_template",
       psd_writer_updates_same_length_imported_text_from_original_type_template},
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
      {"layer_content_revision_ignores_translation_and_tracks_render_content",
       layer_content_revision_ignores_translation_and_tracks_render_content},
      {"tool_brush_draws_color_and_writes_artifact", tool_brush_draws_color_and_writes_artifact},
      {"tool_brush_opacity_and_bounded_layer_expansion_work",
       tool_brush_opacity_and_bounded_layer_expansion_work},
      {"tool_brush_softness_feathers_edge_alpha", tool_brush_softness_feathers_edge_alpha},
      {"tool_brush_repaints_translucent_pixels_without_color_halo",
       tool_brush_repaints_translucent_pixels_without_color_halo},
      {"tool_one_pixel_brush_segment_snaps_fractional_points_to_one_pixel",
       tool_one_pixel_brush_segment_snaps_fractional_points_to_one_pixel},
      {"tool_wide_brush_segment_is_fast_and_writes_artifact",
       tool_wide_brush_segment_is_fast_and_writes_artifact},
      {"tool_brush_segment_accepts_float_endpoints", tool_brush_segment_accepts_float_endpoints},
      {"tool_eraser_clears_alpha_and_writes_artifact", tool_eraser_clears_alpha_and_writes_artifact},
      {"tool_eraser_converts_rgb_layer_to_transparency", tool_eraser_converts_rgb_layer_to_transparency},
      {"tool_smudge_drags_source_pixels_and_writes_artifact", tool_smudge_drags_source_pixels_and_writes_artifact},
      {"tool_line_draws_and_writes_artifact", tool_line_draws_and_writes_artifact},
      {"tool_rectangle_draws_outline_and_writes_artifact", tool_rectangle_draws_outline_and_writes_artifact},
      {"tool_ellipse_draws_and_writes_artifact", tool_ellipse_draws_and_writes_artifact},
      {"tool_filled_ellipse_uses_direct_fill_and_writes_artifact",
       tool_filled_ellipse_uses_direct_fill_and_writes_artifact},
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
      {"plugin_host_and_legacy_probe_work", plugin_host_and_legacy_probe_work},
      {"tile_cache_stores_and_invalidates", tile_cache_stores_and_invalidates},
      {"color_manager_assigns_profiles", color_manager_assigns_profiles},
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
