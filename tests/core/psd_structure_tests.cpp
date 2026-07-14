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
#include "psd_test_support.hpp"
#include "test_groups.hpp"

namespace {

using patchy::test::find_layer_named;
using patchy::test::layer_has_psd_block;
using patchy::test::psd_first_layer_extra_data;
using patchy::test::psd_raw_layer_record_names;
using patchy::test::read_pascal_padded;
using patchy::test::read_u32_be_at;
using patchy::test::rgb_diff_metrics;
using patchy::test::solid_rgb;
using patchy::test::solid_rgba;
using patchy::test::test_blend_if_identity_payload;
using patchy::test::write_ascii4;
using patchy::test::write_pascal_padded;
using patchy::test::write_test_layer_block;

std::vector<std::uint8_t> test_blend_if_payload(std::uint8_t seed) {
  auto payload = test_blend_if_identity_payload();
  for (std::size_t channel = 0; channel < 4U; ++channel) {
    const auto black = static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(channel * 4U));
    const auto offset = channel * 8U;
    const std::array<std::uint8_t, 8> ranges{
        black,
        static_cast<std::uint8_t>(black + 1U),
        static_cast<std::uint8_t>(180U + channel),
        static_cast<std::uint8_t>(220U + channel),
        static_cast<std::uint8_t>(black + 2U),
        static_cast<std::uint8_t>(black + 3U),
        static_cast<std::uint8_t>(190U + channel),
        static_cast<std::uint8_t>(230U + channel),
    };
    std::copy(ranges.begin(), ranges.end(), payload.begin() + static_cast<std::ptrdiff_t>(offset));
  }
  return payload;
}

struct PsdRawLayerRecord {
  std::string name;
  std::uint32_t section_divider_type{0};
  std::vector<std::uint8_t> blending_ranges;

  bool operator==(const PsdRawLayerRecord&) const = default;
};

std::vector<PsdRawLayerRecord> psd_raw_layer_records(std::span<const std::uint8_t> bytes) {
  patchy::psd::BigEndianReader reader(bytes);
  const auto header = patchy::psd::read_header(reader);

  const auto color_mode_length = reader.read_u32();
  reader.skip(color_mode_length);
  const auto image_resource_length = reader.read_u32();
  reader.skip(image_resource_length);

  const auto layer_mask_length =
      header.large_document ? reader.read_u64()
                            : static_cast<std::uint64_t>(reader.read_u32());
  if (layer_mask_length == 0U) {
    return {};
  }
  CHECK(layer_mask_length <= reader.remaining());
  const auto layer_info_length =
      header.large_document ? reader.read_u64()
                            : static_cast<std::uint64_t>(reader.read_u32());
  if (layer_info_length == 0U) {
    return {};
  }
  CHECK(layer_info_length <= reader.remaining());

  const auto layer_count_raw = static_cast<std::int16_t>(reader.read_u16());
  const auto layer_count = layer_count_raw < 0
                               ? -static_cast<int>(layer_count_raw)
                               : static_cast<int>(layer_count_raw);
  std::vector<PsdRawLayerRecord> records;
  records.reserve(static_cast<std::size_t>(layer_count));
  for (int index = 0; index < layer_count; ++index) {
    reader.skip(16);  // bounds
    const auto channel_count = reader.read_u16();
    for (std::uint16_t channel = 0; channel < channel_count; ++channel) {
      reader.skip(2);  // channel id
      reader.skip(header.large_document ? 8U : 4U);  // channel byte length
    }
    reader.skip(12);  // blend signature/key, opacity, clipping, flags, filler

    const auto extra_length = reader.read_u32();
    auto extra_bytes = reader.read_bytes(extra_length);
    patchy::psd::BigEndianReader extra(extra_bytes);
    const auto mask_length = extra.read_u32();
    CHECK(mask_length <= extra.remaining());
    extra.skip(mask_length);
    const auto blending_ranges_length = extra.read_u32();
    CHECK(blending_ranges_length <= extra.remaining());

    PsdRawLayerRecord record;
    record.blending_ranges = extra.read_bytes(blending_ranges_length);
    record.name = read_pascal_padded(extra, 4);
    while (extra.remaining() >= 12U) {
      const auto signature = extra.read_bytes(4);
      if (signature != std::vector<std::uint8_t>{'8', 'B', 'I', 'M'} &&
          signature != std::vector<std::uint8_t>{'8', 'B', '6', '4'}) {
        break;
      }
      const auto key = extra.read_bytes(4);
      const auto payload_length = extra.read_u32();
      CHECK(payload_length <= extra.remaining());
      const auto payload = extra.read_bytes(payload_length);
      if (key == std::vector<std::uint8_t>{'l', 's', 'c', 't'} &&
          payload.size() >= 4U) {
        patchy::psd::BigEndianReader section(payload);
        record.section_divider_type = section.read_u32();
      }
      if ((payload_length % 2U) != 0U) {
        CHECK(extra.remaining() >= 1U);
        extra.skip(1);
      }
    }
    records.push_back(std::move(record));
  }
  return records;
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

void psd_blending_ranges_round_trip_for_layers_and_group_records() {
  const auto pixel_ranges = test_blend_if_payload(1);
  const auto adjustment_ranges = test_blend_if_payload(9);
  const auto outer_group_ranges = test_blend_if_payload(17);
  const auto outer_boundary_ranges = test_blend_if_payload(25);
  const auto inner_group_ranges = test_blend_if_payload(33);
  const auto inner_boundary_ranges = test_blend_if_payload(41);
  const auto nested_child_ranges = test_blend_if_payload(49);

  for (const bool large_document : {false, true}) {
    patchy::Document document(2, 2, patchy::PixelFormat::rgb8());

    patchy::Layer pixel(document.allocate_layer_id(), "Native Pixel",
                        solid_rgba(2, 2, 10, 20, 30, 255));
    pixel.raw_psd_blending_ranges() = pixel_ranges;
    document.add_layer(std::move(pixel));

    patchy::AdjustmentSettings settings;
    settings.kind = patchy::AdjustmentKind::Levels;
    settings.levels.black_input = 12;
    patchy::Layer adjustment(document.allocate_layer_id(), "Native Levels",
                             patchy::LayerKind::Adjustment);
    adjustment.set_bounds(
        patchy::Rect::from_size(document.width(), document.height()));
    patchy::configure_adjustment_layer(adjustment, settings);
    adjustment.raw_psd_blending_ranges() = adjustment_ranges;
    document.add_layer(std::move(adjustment));

    patchy::Layer outer_group(document.allocate_layer_id(), "Outer Folder",
                              patchy::LayerKind::Group);
    patchy::set_layer_group_expanded(outer_group, true);
    outer_group.raw_psd_blending_ranges() = outer_group_ranges;
    outer_group.raw_psd_group_boundary_blending_ranges() =
        outer_boundary_ranges;
    patchy::Layer inner_group(document.allocate_layer_id(), "Inner Folder",
                              patchy::LayerKind::Group);
    patchy::set_layer_group_expanded(inner_group, true);
    inner_group.raw_psd_blending_ranges() = inner_group_ranges;
    inner_group.raw_psd_group_boundary_blending_ranges() =
        inner_boundary_ranges;
    patchy::Layer nested_child(document.allocate_layer_id(), "Nested Child",
                               solid_rgba(2, 2, 40, 50, 60, 255));
    nested_child.raw_psd_blending_ranges() = nested_child_ranges;
    inner_group.add_child(std::move(nested_child));
    outer_group.add_child(std::move(inner_group));
    document.add_layer(std::move(outer_group));

    document.add_pixel_layer("Fresh Pixel Identity",
                             solid_rgba(2, 2, 70, 80, 90, 255));
    patchy::Layer fresh_adjustment(document.allocate_layer_id(),
                                   "Fresh Levels Identity",
                                   patchy::LayerKind::Adjustment);
    fresh_adjustment.set_bounds(
        patchy::Rect::from_size(document.width(), document.height()));
    patchy::configure_adjustment_layer(fresh_adjustment, settings);
    document.add_layer(std::move(fresh_adjustment));
    patchy::Layer fresh_group(document.allocate_layer_id(),
                              "Fresh Folder Identity",
                              patchy::LayerKind::Group);
    patchy::set_layer_group_expanded(fresh_group, true);
    fresh_group.add_child(patchy::Layer(document.allocate_layer_id(),
                                        "Fresh Child Identity",
                                        solid_rgba(2, 2, 100, 110, 120, 255)));
    document.add_layer(std::move(fresh_group));

    const std::vector<PsdRawLayerRecord> expected_records{
        {"Native Pixel", 0, pixel_ranges},
        {"Native Levels", 0, adjustment_ranges},
        {"</Layer group>", 3, outer_boundary_ranges},
        {"</Layer group>", 3, inner_boundary_ranges},
        {"Nested Child", 0, nested_child_ranges},
        {"Inner Folder", 1, inner_group_ranges},
        {"Outer Folder", 1, outer_group_ranges},
        {"Fresh Pixel Identity", 0, {}},
        {"Fresh Levels Identity", 0, {}},
        {"</Layer group>", 3, {}},
        {"Fresh Child Identity", 0, {}},
        {"Fresh Folder Identity", 1, {}},
    };

    const auto assert_raw_records = [&](std::span<const std::uint8_t> bytes) {
      const auto records = psd_raw_layer_records(bytes);
      CHECK(records.size() == expected_records.size());
      for (std::size_t index = 0; index < expected_records.size(); ++index) {
        CHECK(records[index].name == expected_records[index].name);
        CHECK(records[index].section_divider_type ==
              expected_records[index].section_divider_type);
        CHECK(records[index].blending_ranges ==
              expected_records[index].blending_ranges);
      }
    };

    const auto assert_preserved = [&](const patchy::Document& read) {
      CHECK(read.layers().size() == 6);
      CHECK(read.layers()[0].name() == "Native Pixel");
      CHECK(read.layers()[0].raw_psd_blending_ranges() == pixel_ranges);
      CHECK(read.layers()[0].blend_if_payload_status() ==
            patchy::BlendIfPayloadStatus::Supported);
      CHECK(read.layers()[1].name() == "Native Levels");
      CHECK(read.layers()[1].kind() == patchy::LayerKind::Adjustment);
      CHECK(read.layers()[1].raw_psd_blending_ranges() == adjustment_ranges);
      CHECK(read.layers()[1].blend_if_payload_status() ==
            patchy::BlendIfPayloadStatus::Supported);

      const auto& read_outer = read.layers()[2];
      CHECK(read_outer.name() == "Outer Folder");
      CHECK(read_outer.kind() == patchy::LayerKind::Group);
      CHECK(read_outer.raw_psd_blending_ranges() == outer_group_ranges);
      CHECK(read_outer.raw_psd_group_boundary_blending_ranges() ==
            outer_boundary_ranges);
      CHECK(read_outer.children().size() == 1);
      const auto& read_inner = read_outer.children().front();
      CHECK(read_inner.name() == "Inner Folder");
      CHECK(read_inner.kind() == patchy::LayerKind::Group);
      CHECK(read_inner.raw_psd_blending_ranges() == inner_group_ranges);
      CHECK(read_inner.raw_psd_group_boundary_blending_ranges() ==
            inner_boundary_ranges);
      CHECK(read_inner.children().size() == 1);
      CHECK(read_inner.children().front().name() == "Nested Child");
      CHECK(read_inner.children().front().raw_psd_blending_ranges() ==
            nested_child_ranges);

      CHECK(read.layers()[3].name() == "Fresh Pixel Identity");
      CHECK(read.layers()[3].raw_psd_blending_ranges().empty());
      CHECK(read.layers()[3].blend_if_payload_status() ==
            patchy::BlendIfPayloadStatus::Empty);
      CHECK(read.layers()[4].name() == "Fresh Levels Identity");
      CHECK(read.layers()[4].kind() == patchy::LayerKind::Adjustment);
      CHECK(read.layers()[4].raw_psd_blending_ranges().empty());
      CHECK(read.layers()[4].blend_if_payload_status() ==
            patchy::BlendIfPayloadStatus::Empty);
      const auto& read_fresh_group = read.layers()[5];
      CHECK(read_fresh_group.name() == "Fresh Folder Identity");
      CHECK(read_fresh_group.kind() == patchy::LayerKind::Group);
      CHECK(read_fresh_group.raw_psd_blending_ranges().empty());
      CHECK(read_fresh_group.raw_psd_group_boundary_blending_ranges().empty());
      CHECK(read_fresh_group.blend_if_payload_status() ==
            patchy::BlendIfPayloadStatus::Empty);
      CHECK(read_fresh_group.children().size() == 1);
      CHECK(read_fresh_group.children().front().name() ==
            "Fresh Child Identity");
      CHECK(read_fresh_group.children()
                .front()
                .raw_psd_blending_ranges()
                .empty());
    };

    const patchy::psd::WriteOptions options{large_document};
    const auto first_bytes =
        patchy::psd::DocumentIo::write_layered_rgb8(document, options);
    assert_raw_records(first_bytes);
    const auto first_read = patchy::psd::DocumentIo::read(first_bytes);
    assert_preserved(first_read);

    const auto second_bytes =
        patchy::psd::DocumentIo::write_layered_rgb8(first_read, options);
    assert_raw_records(second_bytes);
    const auto second_read = patchy::psd::DocumentIo::read(second_bytes);
    assert_preserved(second_read);
  }
}

void psd_photoshop_blend_if_4b_fixture_round_trips_and_matches_render() {
  const auto identity = test_blend_if_identity_payload();
  const std::vector<std::uint8_t> group_ranges{
      0x07, 0x1b, 0xd5, 0xf3, 0x0f, 0x2d, 0xc3, 0xe7,  // Gray
      0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,  // Red
      0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,  // Green
      0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,  // Blue
      0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,  // identity tail
  };
  const std::vector<std::uint8_t> levels_ranges{
      0x09, 0x1d, 0xd3, 0xf1, 0x11, 0x2f, 0xc1, 0xe5,  // Gray
      0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,  // Red
      0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,  // Green
      0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,  // Blue
      0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,  // identity tail
  };
  const std::vector<std::uint8_t> normal_ranges{
      0x0b, 0x25, 0xc9, 0xef, 0x13, 0x35, 0xbb, 0xe3,  // Gray
      0x03, 0x21, 0xcb, 0xe9, 0x0d, 0x2b, 0xc1, 0xdf,  // Red
      0x05, 0x23, 0xcd, 0xeb, 0x0f, 0x2d, 0xc3, 0xe1,  // Green
      0x07, 0x25, 0xcf, 0xed, 0x11, 0x2f, 0xc5, 0xe3,  // Blue
      0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff,  // identity tail
  };

  const auto assert_modeled_payload = [](const patchy::Layer& layer,
                                          const std::vector<std::uint8_t>& expected) {
    CHECK(expected.size() == 40U);
    CHECK(layer.raw_psd_blending_ranges() == expected);
    const auto decoded = patchy::decode_layer_blend_if(expected);
    CHECK(decoded.status == patchy::BlendIfPayloadStatus::Supported);
    CHECK(layer.blend_if_payload_status() == patchy::BlendIfPayloadStatus::Supported);
    CHECK(layer.blend_if() == decoded.settings);
  };

  const auto assert_document = [&](const patchy::Document& document) {
    CHECK(document.layers().size() == 4U);
    const auto& background = document.layers()[0];
    CHECK(background.name() == "Background");
    CHECK(background.kind() == patchy::LayerKind::Pixel);
    assert_modeled_payload(background, identity);

    const auto& group = document.layers()[1];
    CHECK(group.name() == "Blend If Group");
    CHECK(group.kind() == patchy::LayerKind::Group);
    assert_modeled_payload(group, group_ranges);
    CHECK(group.raw_psd_group_boundary_blending_ranges() == identity);
    CHECK(group.raw_psd_group_boundary_blending_ranges().size() == 40U);
    CHECK(group.children().size() == 1U);
    CHECK(group.children().front().name() == "Group Child");
    CHECK(group.children().front().kind() == patchy::LayerKind::Pixel);
    assert_modeled_payload(group.children().front(), identity);

    const auto& levels = document.layers()[2];
    CHECK(levels.name() == "Blend If Levels");
    CHECK(levels.kind() == patchy::LayerKind::Adjustment);
    const auto levels_settings = patchy::adjustment_settings_from_layer(levels);
    CHECK(levels_settings.has_value());
    CHECK(levels_settings->kind == patchy::AdjustmentKind::Levels);
    CHECK(layer_has_psd_block(levels, "levl"));
    assert_modeled_payload(levels, levels_ranges);

    const auto& normal = document.layers()[3];
    CHECK(normal.name() == "Blend If Normal");
    CHECK(normal.kind() == patchy::LayerKind::Pixel);
    assert_modeled_payload(normal, normal_ranges);
  };

  const auto psd_path =
      patchy::test::committed_psd_fixture_path("photoshop-blend-if-4b-roundtrip.psd");
  const auto bmp_path = psd_path.parent_path() / "photoshop-blend-if-4b-render.bmp";
  CHECK(std::filesystem::exists(psd_path));
  CHECK(std::filesystem::exists(bmp_path));

  const auto editable = patchy::psd::DocumentIo::read_file(psd_path);
  assert_document(editable);

  const auto resaved_bytes = patchy::psd::DocumentIo::write_layered_rgb8(editable);
  const std::vector<PsdRawLayerRecord> expected_records{
      {"Background", 0, identity},
      {"</Layer group>", 3, identity},
      {"Group Child", 0, identity},
      {"Blend If Group", 1, group_ranges},
      {"Blend If Levels", 0, levels_ranges},
      {"Blend If Normal", 0, normal_ranges},
  };
  const auto raw_records = psd_raw_layer_records(resaved_bytes);
  CHECK(raw_records.size() == expected_records.size());
  for (std::size_t index = 0; index < expected_records.size(); ++index) {
    CHECK(raw_records[index].name == expected_records[index].name);
    CHECK(raw_records[index].section_divider_type == expected_records[index].section_divider_type);
    CHECK(raw_records[index].blending_ranges == expected_records[index].blending_ranges);
  }
  const auto reread = patchy::psd::DocumentIo::read(resaved_bytes);
  assert_document(reread);

  const auto photoshop_render = patchy::bmp::DocumentIo::read_file(bmp_path);
  const auto reference_flat = patchy::Compositor{}.flatten_rgb8(photoshop_render);
  const auto editable_flat = patchy::Compositor{}.flatten_rgb8(editable);
  const auto metrics = rgb_diff_metrics(reference_flat, editable_flat);
  CHECK(metrics.max_channel_delta <= 2);
  CHECK(metrics.mean_abs_channel_delta <= 0.60);
}

void psd_round_trips_clipping_flag() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(2, 2, 200, 60, 60));
  document.add_pixel_layer("Clip", solid_rgb(2, 2, 40, 200, 90));
  document.layers()[1].set_clipped(true);

  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Levels;
  settings.levels.black_input = 16;
  patchy::Layer adjustment(document.allocate_layer_id(), "Clip Levels", patchy::LayerKind::Adjustment);
  adjustment.set_bounds(patchy::Rect::from_size(document.width(), document.height()));
  patchy::configure_adjustment_layer(adjustment, settings);
  adjustment.set_clipped(true);
  document.add_layer(std::move(adjustment));

  patchy::Layer group(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  group.add_child(patchy::Layer(document.allocate_layer_id(), "Group Base", solid_rgba(2, 2, 180, 20, 20, 255)));
  patchy::Layer group_clip(document.allocate_layer_id(), "Group Clip", solid_rgba(2, 2, 20, 40, 220, 255));
  group_clip.set_clipped(true);
  group.add_child(std::move(group_clip));
  document.add_layer(std::move(group));

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 4);
  CHECK(!read.layers()[0].clipped());
  CHECK(read.layers()[1].clipped());
  CHECK(read.layers()[2].clipped());
  CHECK(read.layers()[2].kind() == patchy::LayerKind::Adjustment);
  const auto& folder = read.layers()[3];
  CHECK(folder.kind() == patchy::LayerKind::Group);
  CHECK(!folder.clipped());
  CHECK(folder.children().size() == 2);
  CHECK(!folder.children()[0].clipped());
  CHECK(folder.children()[1].clipped());

  // Second pass proves the writer emits the byte the reader consumed.
  const auto read_again = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(read));
  CHECK(read_again.layers()[1].clipped());
  CHECK(read_again.layers()[3].children()[1].clipped());
}

void psd_clipped_first_in_group_round_trips_and_renders_unclipped() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(2, 2, 255, 255, 255));

  patchy::Layer group(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  patchy::Layer orphan(document.allocate_layer_id(), "Orphan", solid_rgba(2, 2, 10, 130, 250, 255));
  orphan.set_clipped(true);  // nothing below it inside the group: no base
  group.add_child(std::move(orphan));
  document.add_layer(std::move(group));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(0, 0)[0] == 10);
  CHECK(flattened.pixel(0, 0)[2] == 250);

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto read = patchy::psd::DocumentIo::read(bytes);
  const auto& folder = read.layers().back();
  CHECK(folder.kind() == patchy::LayerKind::Group);
  CHECK(folder.children().size() == 1);
  CHECK(folder.children().front().clipped());
  const auto read_flattened = patchy::Compositor{}.flatten_rgb8(read);
  CHECK(read_flattened.pixel(0, 0)[0] == 10);
  CHECK(read_flattened.pixel(0, 0)[2] == 250);
}

void psd_photoshop_clipping_fixture_matches_composite() {
  const auto path = patchy::test::committed_psd_fixture_path("photoshop-clipping-mask.psd");
  CHECK(std::filesystem::exists(path));

  const auto editable = patchy::psd::DocumentIo::read_file(path);
  CHECK(editable.layers().size() == 4);
  const auto* base = find_layer_named(editable.layers(), "Clip Base");
  const auto* member = find_layer_named(editable.layers(), "Clip Multiply");
  const auto* levels = find_layer_named(editable.layers(), "Levels 1");
  CHECK(base != nullptr);
  CHECK(member != nullptr);
  CHECK(levels != nullptr);
  CHECK(!base->clipped());
  CHECK(member->clipped());
  CHECK(member->blend_mode() == patchy::BlendMode::Multiply);
  CHECK(levels->clipped());
  CHECK(levels->kind() == patchy::LayerKind::Adjustment);

  patchy::psd::ReadOptions flat_options;
  flat_options.prefer_flat_composite = true;
  const auto reference_flat =
      patchy::Compositor{}.flatten_rgb8(patchy::psd::DocumentIo::read_file(path, flat_options));
  const auto patchy_flat = patchy::Compositor{}.flatten_rgb8(editable);
  const auto metrics = rgb_diff_metrics(reference_flat, patchy_flat);
  // Photoshop 2026's own render of this document matches Patchy's clipping
  // compositor exactly; the semi-transparent base region pins the
  // blend-against-base-color (destination alpha 1) model.
  CHECK(metrics.max_channel_delta == 0);
}

void compositor_clips_layer_to_base_alpha() {
  patchy::Document document(8, 8, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(8, 8, 255, 255, 255));

  patchy::Layer base(document.allocate_layer_id(), "Base", solid_rgba(4, 4, 200, 30, 30, 255));
  base.set_bounds(patchy::Rect{2, 2, 4, 4});
  document.add_layer(std::move(base));

  patchy::Layer clipped(document.allocate_layer_id(), "Clipped", solid_rgba(8, 8, 20, 200, 60, 255));
  clipped.set_clipped(true);
  document.add_layer(std::move(clipped));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.pixel(4, 4)[1] == 200);  // member shows over the base
  CHECK(flattened.pixel(4, 4)[0] == 20);
  CHECK(flattened.pixel(0, 0)[0] == 255);  // outside the base: untouched white
  CHECK(flattened.pixel(0, 0)[1] == 255);
  CHECK(flattened.pixel(7, 7)[1] == 255);
  CHECK(flattened.pixel(1, 4)[0] == 255);  // just left of the base column
  CHECK(flattened.pixel(2, 4)[1] == 200);  // first base column
}

void compositor_clipped_layer_uses_own_blend_mode_and_opacity() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(2, 2, 40, 40, 40));
  document.add_pixel_layer("Base", solid_rgba(2, 2, 180, 180, 180, 255));

  patchy::Layer clipped(document.allocate_layer_id(), "Clipped", solid_rgba(2, 2, 200, 60, 100, 255));
  clipped.set_clipped(true);
  clipped.set_blend_mode(patchy::BlendMode::Multiply);
  clipped.set_opacity(0.5F);
  document.add_layer(std::move(clipped));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  // Inside the isolated group the member blends against the base's color at
  // full strength (destination alpha 1), with its own mode and opacity.
  const std::array<std::uint8_t, 3> base_rgb{180, 180, 180};
  const std::array<std::uint8_t, 3> member_rgb{200, 60, 100};
  const auto expected =
      patchy::composite_blended_rgb(member_rgb, base_rgb, patchy::BlendMode::Multiply, 0.5F, 1.0F);
  CHECK(flattened.pixel(0, 0)[0] == expected[0]);
  CHECK(flattened.pixel(0, 0)[1] == expected[1]);
  CHECK(flattened.pixel(0, 0)[2] == expected[2]);
}

void compositor_clip_group_blends_with_base_mode_and_opacity() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(2, 2, 120, 200, 80));

  patchy::Layer base(document.allocate_layer_id(), "Base", solid_rgba(2, 2, 180, 180, 180, 255));
  base.set_blend_mode(patchy::BlendMode::Multiply);
  base.set_opacity(0.5F);
  document.add_layer(std::move(base));

  patchy::Layer clipped(document.allocate_layer_id(), "Clipped", solid_rgba(2, 2, 250, 40, 40, 255));
  clipped.set_clipped(true);
  document.add_layer(std::move(clipped));

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  // The clipped member replaces the base color inside the group (Normal at
  // full strength); the ensemble then blends into the canvas as a unit with
  // the BASE's blend mode and opacity.
  const std::array<std::uint8_t, 3> group_rgb{250, 40, 40};
  const std::array<std::uint8_t, 3> backdrop_rgb{120, 200, 80};
  const auto expected =
      patchy::composite_blended_rgb(group_rgb, backdrop_rgb, patchy::BlendMode::Multiply, 0.5F, 1.0F);
  CHECK(flattened.pixel(1, 1)[0] == expected[0]);
  CHECK(flattened.pixel(1, 1)[1] == expected[1]);
  CHECK(flattened.pixel(1, 1)[2] == expected[2]);
}

void compositor_clipped_adjustment_affects_base_only() {
  patchy::AdjustmentSettings warm;
  warm.kind = patchy::AdjustmentKind::ColorBalance;
  warm.color_balance = patchy::ColorBalanceAdjustment{50, 0, 0};

  const auto build_document = [&warm](bool clipped) {
    patchy::Document document(8, 8, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(8, 8, 100, 100, 100));
    patchy::Layer base(document.allocate_layer_id(), "Base", solid_rgba(4, 4, 0, 200, 0, 255));
    base.set_bounds(patchy::Rect{2, 2, 4, 4});
    document.add_layer(std::move(base));
    patchy::Layer adjustment(document.allocate_layer_id(), "Warmth", patchy::LayerKind::Adjustment);
    adjustment.set_bounds(patchy::Rect::from_size(8, 8));
    patchy::configure_adjustment_layer(adjustment, warm);
    adjustment.set_clipped(clipped);
    document.add_layer(std::move(adjustment));
    return document;
  };

  const auto clipped_flat = patchy::Compositor{}.flatten_rgb8(build_document(true));
  CHECK(clipped_flat.pixel(4, 4)[0] == 128);   // base square adjusted (0 + 128)
  CHECK(clipped_flat.pixel(4, 4)[1] == 200);
  CHECK(clipped_flat.pixel(0, 0)[0] == 100);   // backdrop untouched
  CHECK(clipped_flat.pixel(7, 7)[0] == 100);

  const auto unclipped_flat = patchy::Compositor{}.flatten_rgb8(build_document(false));
  CHECK(unclipped_flat.pixel(4, 4)[0] == 128);
  CHECK(unclipped_flat.pixel(0, 0)[0] == 228);  // backdrop adjusted too (100 + 128)
}

void compositor_clipping_run_edge_cases() {
  // (a) A clipped layer with nothing below renders unclipped.
  {
    patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
    patchy::Layer orphan(document.allocate_layer_id(), "Orphan", solid_rgba(2, 2, 10, 130, 250, 255));
    orphan.set_clipped(true);
    document.add_layer(std::move(orphan));
    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    CHECK(flattened.pixel(0, 0)[2] == 250);
  }
  // (b) A clipped layer above a group renders unclipped (groups cannot host).
  {
    patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
    patchy::Layer group(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
    group.add_child(patchy::Layer(document.allocate_layer_id(), "Inside", solid_rgba(1, 1, 255, 0, 0, 255)));
    document.add_layer(std::move(group));
    patchy::Layer clipped(document.allocate_layer_id(), "Clipped", solid_rgba(2, 2, 10, 130, 250, 255));
    clipped.set_clipped(true);
    document.add_layer(std::move(clipped));
    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    CHECK(flattened.pixel(1, 1)[2] == 250);  // covers the whole canvas, not just the group
  }
  // (c) A clipped layer above an adjustment layer renders unclipped.
  {
    patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
    patchy::AdjustmentSettings settings;
    settings.kind = patchy::AdjustmentKind::ColorBalance;
    settings.color_balance = patchy::ColorBalanceAdjustment{50, 0, 0};
    patchy::Layer adjustment(document.allocate_layer_id(), "Adjust", patchy::LayerKind::Adjustment);
    adjustment.set_bounds(patchy::Rect::from_size(2, 2));
    patchy::configure_adjustment_layer(adjustment, settings);
    document.add_layer(std::move(adjustment));
    patchy::Layer clipped(document.allocate_layer_id(), "Clipped", solid_rgba(2, 2, 10, 130, 250, 255));
    clipped.set_clipped(true);
    document.add_layer(std::move(clipped));
    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    CHECK(flattened.pixel(0, 0)[2] == 250);
  }
  // (d) A hidden base hides every member; (e) a hidden member is skipped.
  {
    patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(2, 2, 255, 255, 255));
    patchy::Layer base(document.allocate_layer_id(), "Base", solid_rgba(2, 2, 200, 30, 30, 255));
    base.set_visible(false);
    document.add_layer(std::move(base));
    patchy::Layer member(document.allocate_layer_id(), "Member", solid_rgba(2, 2, 10, 130, 250, 255));
    member.set_clipped(true);
    document.add_layer(std::move(member));
    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    CHECK(flattened.pixel(0, 0)[0] == 255);  // hidden base hides the whole group
  }
  {
    patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(2, 2, 200, 30, 30));
    patchy::Layer hidden_member(document.allocate_layer_id(), "Hidden", solid_rgba(2, 2, 0, 255, 0, 255));
    hidden_member.set_clipped(true);
    hidden_member.set_visible(false);
    document.add_layer(std::move(hidden_member));
    patchy::Layer member(document.allocate_layer_id(), "Member", solid_rgba(2, 2, 10, 130, 250, 255));
    member.set_clipped(true);
    document.add_layer(std::move(member));
    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    CHECK(flattened.pixel(0, 0)[2] == 250);  // visible member still applies over base
    CHECK(flattened.pixel(0, 0)[1] == 130);
  }
  // (f) A run inside a group's children behaves like a root run.
  {
    patchy::Document document(8, 8, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(8, 8, 255, 255, 255));
    patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
    patchy::Layer base(document.allocate_layer_id(), "Base", solid_rgba(4, 4, 200, 30, 30, 255));
    base.set_bounds(patchy::Rect{2, 2, 4, 4});
    folder.add_child(std::move(base));
    patchy::Layer member(document.allocate_layer_id(), "Member", solid_rgba(8, 8, 10, 130, 250, 255));
    member.set_clipped(true);
    folder.add_child(std::move(member));
    document.add_layer(std::move(folder));
    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    CHECK(flattened.pixel(4, 4)[2] == 250);
    CHECK(flattened.pixel(0, 0)[0] == 255);
  }
}

void compositor_clipping_is_thread_count_stable() {
  // Big enough (>= 4 Mpx, >= 2 strips) to engage the parallel strip renderer;
  // the clip run spans every strip boundary.
  patchy::Document document(2400, 2000, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(2400, 2000, 30, 60, 90));
  patchy::Layer base(document.allocate_layer_id(), "Base", solid_rgba(1200, 2000, 200, 180, 40, 255));
  base.set_bounds(patchy::Rect{600, 0, 1200, 2000});
  base.set_opacity(0.75F);
  document.add_layer(std::move(base));
  patchy::Layer member(document.allocate_layer_id(), "Member", solid_rgba(2400, 2000, 60, 200, 160, 255));
  member.set_clipped(true);
  member.set_blend_mode(patchy::BlendMode::Multiply);
  document.add_layer(std::move(member));

  const auto parallel = patchy::Compositor{}.flatten_rgb8(document);
#ifdef _WIN32
  _putenv_s("PATCHY_RENDER_SINGLE_THREADED", "1");
#else
  setenv("PATCHY_RENDER_SINGLE_THREADED", "1", 1);
#endif
  const auto sequential = patchy::Compositor{}.flatten_rgb8(document);
#ifdef _WIN32
  _putenv_s("PATCHY_RENDER_SINGLE_THREADED", "");
#else
  unsetenv("PATCHY_RENDER_SINGLE_THREADED");
#endif
  CHECK(parallel.width() == sequential.width());
  CHECK(parallel.height() == sequential.height());
  bool identical = true;
  for (std::int32_t y = 0; y < parallel.height() && identical; ++y) {
    identical = std::memcmp(parallel.pixel(0, y), sequential.pixel(0, y),
                            static_cast<std::size_t>(parallel.width()) * 3U) == 0;
  }
  CHECK(identical);
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

void psd_fill_opacity_block_round_trips_and_omits_default() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer("Fill", solid_rgba(1, 1, 20, 40, 60, 255));
  layer.set_fill_opacity(37.0F / 100.0F);
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const std::array<std::uint8_t, 12> header{'8', 'B', 'I', 'M', 'i', 'O', 'p', 'a', 0, 0, 0, 4};
  const auto found = std::search(bytes.begin(), bytes.end(), header.begin(), header.end());
  CHECK(found != bytes.end());
  CHECK(*(found + 12) == 94U);

  auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 1U);
  CHECK(std::abs(read.layers().front().fill_opacity() - 94.0F / 255.0F) < 0.0001F);
  read.layers().front().set_fill_opacity(1.0F);
  const auto default_bytes = patchy::psd::DocumentIo::write_layered_rgb8(read);
  CHECK(std::search(default_bytes.begin(), default_bytes.end(), header.begin(), header.end()) ==
        default_bytes.end());
}

}  // namespace

std::vector<patchy::test::TestCase> psd_structure_tests() {
  return {
      {"psd_reader_tolerates_legacy_patchy_top_to_bottom_background_files",
       psd_reader_tolerates_legacy_patchy_top_to_bottom_background_files},
      {"psd_reader_preserves_layer_group_hierarchy", psd_reader_preserves_layer_group_hierarchy},
      {"psd_writer_round_trips_layer_groups", psd_writer_round_trips_layer_groups},
      {"psd_blending_ranges_round_trip_for_layers_and_group_records",
       psd_blending_ranges_round_trip_for_layers_and_group_records},
      {"psd_photoshop_blend_if_4b_fixture_round_trips_and_matches_render",
       psd_photoshop_blend_if_4b_fixture_round_trips_and_matches_render},
      {"psd_round_trips_clipping_flag", psd_round_trips_clipping_flag},
      {"psd_clipped_first_in_group_round_trips_and_renders_unclipped",
       psd_clipped_first_in_group_round_trips_and_renders_unclipped},
      {"psd_photoshop_clipping_fixture_matches_composite", psd_photoshop_clipping_fixture_matches_composite},
      {"compositor_clips_layer_to_base_alpha", compositor_clips_layer_to_base_alpha},
      {"compositor_clipped_layer_uses_own_blend_mode_and_opacity",
       compositor_clipped_layer_uses_own_blend_mode_and_opacity},
      {"compositor_clip_group_blends_with_base_mode_and_opacity",
       compositor_clip_group_blends_with_base_mode_and_opacity},
      {"compositor_clipped_adjustment_affects_base_only", compositor_clipped_adjustment_affects_base_only},
      {"compositor_clipping_run_edge_cases", compositor_clipping_run_edge_cases},
      {"compositor_clipping_is_thread_count_stable", compositor_clipping_is_thread_count_stable},
      {"psd_ipad_main_v04_preserves_folders_if_available", psd_ipad_main_v04_preserves_folders_if_available},
      {"psd_writer_preserves_layer_additional_blocks_and_long_names",
       psd_writer_preserves_layer_additional_blocks_and_long_names},
      {"psd_fill_opacity_block_round_trips_and_omits_default",
       psd_fill_opacity_block_round_trips_and_omits_default},
  };
}
