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
using patchy::test::psd_layer_block_payload;
using patchy::test::psd_layer_extra_data;
using patchy::test::psd_raw_layer_record_names;
using patchy::test::rgb_diff_metrics;
using patchy::test::solid_rgb;
using patchy::test::solid_rgba;
using patchy::test::write_ascii4;
using patchy::test::write_pascal_padded;
using patchy::test::write_rgb8_bmp_artifact;
using patchy::test::write_test_layer_block;

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

void curves_control_points_normalize_and_build_composed_luts() {
  const auto normalized = patchy::normalized_curve_control_points(
      {{255, 260}, {64, 20}, {64, 45}, {-8, 12}, {128, 190}});
  const patchy::CurveControlPoints expected{{0, 12}, {64, 45}, {128, 190}, {255, 255}};
  CHECK(normalized == expected);

  const auto identity = patchy::build_curve_lut({{0, 0}, {128, 128}, {255, 255}});
  for (std::size_t value = 0; value < identity.size(); ++value) {
    CHECK(identity[value] == value);
  }
  const auto endpoint_clamped = patchy::build_curve_lut({{16, 9}, {240, 246}});
  for (std::size_t value = 0; value <= 16U; ++value) {
    CHECK(endpoint_clamped[value] == 9U);
  }
  for (std::size_t value = 240U; value < 256U; ++value) {
    CHECK(endpoint_clamped[value] == 246U);
  }

  patchy::CurvesAdjustment curves;
  curves.rgb = {{0, 8}, {64, 35}, {128, 190}, {255, 245}};
  curves.red = {{0, 20}, {255, 230}};
  curves.green = {{0, 0}, {96, 120}, {255, 255}};
  curves.blue = {{0, 255}, {255, 0}};
  const auto master = patchy::build_curve_lut(curves.rgb);
  const auto red = patchy::build_curve_lut(curves.red);
  const auto green = patchy::build_curve_lut(curves.green);
  const auto blue = patchy::build_curve_lut(curves.blue);
  const auto composed = patchy::build_curves_lut(curves);
  for (std::size_t value = 0; value < 256U; ++value) {
    CHECK(composed.red[value] == master[red[value]]);
    CHECK(composed.green[value] == master[green[value]]);
    CHECK(composed.blue[value] == master[blue[value]]);
  }

  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Curves;
  settings.curves.rgb = {{0, 0}, {32, 32}, {128, 128}, {255, 255}};
  CHECK(!patchy::adjustment_has_effect(settings));
  settings.curves.red = {{0, 0}, {128, 160}, {255, 255}};
  CHECK(patchy::adjustment_has_effect(settings));
  const auto adjusted = patchy::apply_adjustment_to_color({128, 128, 128}, settings);
  CHECK(adjusted.red == patchy::build_curves_lut(settings.curves).red[128]);
  settings.curves = {};
  const auto identity_after_settings_change = patchy::apply_adjustment_to_color({128, 128, 128}, settings);
  CHECK(identity_after_settings_change.red == 128U);
  CHECK(identity_after_settings_change.green == 128U);
  CHECK(identity_after_settings_change.blue == 128U);
}

void curves_lut_matches_photoshop_2026_calibration() {
  // Captured by applying each curve to an exact 0..255 RGB ramp in Photoshop
  // 2026 and exporting an uncompressed 24-bit BMP. These hashes pin every LUT
  // byte, including spline boundary behavior, rounding and channel order.
  const auto hash_lut = [](const std::array<std::uint8_t, 256>& lut) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto value : lut) {
      hash ^= value;
      hash *= 1099511628211ULL;
    }
    return hash;
  };
  const auto check_curve = [&hash_lut](const char* name, const patchy::CurveControlPoints& points,
                                      std::uint64_t expected) {
    const auto hash = hash_lut(patchy::build_curve_lut(points));
    if (hash != expected) {
      std::cout << "Photoshop Curves LUT hash for " << name << ": 0x" << std::hex << hash << std::dec
                << "\n";
    }
    CHECK(hash == expected);
  };

  check_curve("identity", {{0, 0}, {255, 255}}, 0x16d173bdfcdae583ULL);
  check_curve("invert", {{0, 255}, {255, 0}}, 0x7ec50c4b6e7dc283ULL);
  check_curve("legacy", {{0, 0}, {128, 220}, {255, 255}}, 0x011a874190be8f3aULL);
  check_curve("asymmetric", {{0, 0}, {64, 30}, {128, 200}, {200, 180}, {255, 255}},
              0x6bf59b8d1add82b7ULL);
  check_curve("nonuniform", {{0, 0}, {17, 64}, {91, 80}, {173, 230}, {255, 255}},
              0x25b500895b388a20ULL);
  check_curve("plateau", {{0, 0}, {64, 64}, {128, 64}, {192, 192}, {255, 255}},
              0x7eff683aa1d494ebULL);
  check_curve("tight", {{0, 0}, {127, 0}, {128, 255}, {255, 255}}, 0xe8c09cd965f43303ULL);
  check_curve("moved endpoints", {{16, 0}, {128, 160}, {240, 255}}, 0x2c49d26da6d1523eULL);
  check_curve("alternating 16",
              {{0, 0},     {17, 48},   {34, 20},   {51, 92},   {68, 55},   {85, 140},
               {102, 80},  {119, 180}, {136, 110}, {153, 215}, {170, 135}, {187, 235},
               {204, 170}, {221, 248}, {238, 205}, {255, 255}},
              0x12dd8b9c1d5dc5b8ULL);

  patchy::CurvesAdjustment combined;
  combined.rgb = {{0, 8}, {64, 35}, {128, 190}, {255, 245}};
  combined.red = {{0, 20}, {80, 45}, {160, 225}, {255, 230}};
  const auto combined_lut = patchy::build_curves_lut(combined);
  CHECK(hash_lut(combined_lut.red) == 0xe19b4c2c6f440cb5ULL);
}

void curves_eyedroppers_rebuild_neutral_component_curves() {
  patchy::CurvesEyedropperSamples samples;
  samples.black = patchy::RgbColor{20, 30, 40};
  samples.gray = patchy::RgbColor{100, 110, 120};
  samples.white = patchy::RgbColor{220, 230, 240};
  const auto curves = patchy::curves_adjustment_from_eyedropper_samples(samples);
  CHECK(curves.rgb == patchy::CurveControlPoints({{0, 0}, {255, 255}}));
  CHECK(curves.red == patchy::CurveControlPoints({{20, 0}, {100, 128}, {220, 255}}));
  CHECK(curves.green == patchy::CurveControlPoints({{30, 0}, {110, 128}, {230, 255}}));
  CHECK(curves.blue == patchy::CurveControlPoints({{40, 0}, {120, 128}, {240, 255}}));

  const auto lut = patchy::build_curves_lut(curves);
  CHECK(lut.red[20] == 0U);
  CHECK(lut.green[30] == 0U);
  CHECK(lut.blue[40] == 0U);
  CHECK(lut.red[100] == 128U);
  CHECK(lut.green[110] == 128U);
  CHECK(lut.blue[120] == 128U);
  CHECK(lut.red[220] == 255U);
  CHECK(lut.green[230] == 255U);
  CHECK(lut.blue[240] == 255U);

  patchy::CurvesEyedropperSamples degenerate;
  degenerate.black = patchy::RgbColor{255, 255, 255};
  degenerate.white = patchy::RgbColor{0, 0, 0};
  const auto bounded = patchy::curves_adjustment_from_eyedropper_samples(degenerate);
  for (const auto channel : {patchy::CurvesChannel::Red, patchy::CurvesChannel::Green,
                             patchy::CurvesChannel::Blue}) {
    const auto& points = patchy::curve_points_for_channel(bounded, channel);
    CHECK(points.size() == 2U);
    CHECK(points[0].input < points[1].input);
  }
}

void acv_v4_reads_photoshop_rgb_order_and_writes_canonical_bytes() {
  patchy::CurvesAdjustment expected;
  expected.rgb = {{0, 0}, {64, 48}, {255, 255}};
  expected.red = {{0, 12}, {127, 160}, {255, 240}};
  expected.green = {{0, 0}, {255, 255}};
  expected.blue = {{0, 255}, {255, 0}};

  patchy::psd::BigEndianWriter photoshop;
  photoshop.write_u16(4);  // counted ACV version
  photoshop.write_u16(5);  // Photoshop RGB: master, R, G, B, reserved identity
  const auto add_curve = [&photoshop](const patchy::CurveControlPoints& points) {
    photoshop.write_u16(static_cast<std::uint16_t>(points.size()));
    for (const auto& point : points) {
      photoshop.write_u16(static_cast<std::uint16_t>(point.output));
      photoshop.write_u16(static_cast<std::uint16_t>(point.input));
    }
  };
  add_curve(expected.rgb);
  add_curve(expected.red);
  add_curve(expected.green);
  add_curve(expected.blue);
  add_curve({{0, 0}, {255, 255}});

  CHECK(patchy::acv::read(photoshop.bytes()) == expected);
  CHECK(patchy::acv::write(expected) == photoshop.bytes());

  std::filesystem::create_directories("test-artifacts");
  const std::filesystem::path path = "test-artifacts/curves-roundtrip.acv";
  patchy::acv::write_file(path, expected);
  CHECK(patchy::acv::read_file(path) == expected);

  // Valid single-master presets apply identity to unspecified RGB channels.
  patchy::psd::BigEndianWriter master_only;
  master_only.write_u16(4);
  master_only.write_u16(1);
  master_only.write_u16(3);
  master_only.write_u16(0);
  master_only.write_u16(0);
  master_only.write_u16(190);
  master_only.write_u16(128);
  master_only.write_u16(255);
  master_only.write_u16(255);
  patchy::CurvesAdjustment master_expected;
  master_expected.rgb = {{0, 0}, {128, 190}, {255, 255}};
  CHECK(patchy::acv::read(master_only.bytes()) == master_expected);
}

void acv_v1_reads_photoshop_bitmap_and_indexed_extension() {
  const auto add_curve = [](patchy::psd::BigEndianWriter& writer,
                            const patchy::CurveControlPoints& points) {
    writer.write_u16(static_cast<std::uint16_t>(points.size()));
    for (const auto& point : points) {
      writer.write_u16(static_cast<std::uint16_t>(point.output));
      writer.write_u16(static_cast<std::uint16_t>(point.input));
    }
  };

  // Photoshop 2026 native `curv` bodies use a BE u32 bitmap. The indexed
  // extension repeats the channels and is authoritative.
  patchy::psd::BigEndianWriter current;
  current.write_u16(1);
  current.write_u32(0x0000000bU);  // Composite, Red, Blue.
  add_curve(current, {{0, 0}, {128, 180}, {255, 255}});
  add_curve(current, {{0, 10}, {255, 245}});
  add_curve(current, {{0, 250}, {255, 5}});
  current.write_bytes(std::array<std::uint8_t, 4>{'C', 'r', 'v', ' '});
  current.write_u16(4);
  current.write_u32(4);
  current.write_u16(0);
  add_curve(current, {{0, 0}, {128, 190}, {255, 255}});
  current.write_u16(1);
  add_curve(current, {{0, 12}, {255, 240}});
  current.write_u16(2);
  add_curve(current, {{0, 4}, {96, 120}, {255, 251}});
  current.write_u16(3);
  add_curve(current, {{0, 255}, {255, 0}});

  patchy::CurvesAdjustment current_expected;
  current_expected.rgb = {{0, 0}, {128, 190}, {255, 255}};
  current_expected.red = {{0, 12}, {255, 240}};
  current_expected.green = {{0, 4}, {96, 120}, {255, 251}};
  current_expected.blue = {{0, 255}, {255, 0}};
  CHECK(patchy::acv::read(current.bytes()) == current_expected);

  // Adobe's published ACV table documents a legacy u16 bitmap. Missing
  // component records remain identity.
  patchy::psd::BigEndianWriter legacy;
  legacy.write_u16(1);
  legacy.write_u16(0x0005U);  // Composite and Green.
  add_curve(legacy, {{0, 0}, {128, 175}, {255, 255}});
  add_curve(legacy, {{0, 3}, {255, 252}});
  patchy::CurvesAdjustment legacy_expected;
  legacy_expected.rgb = {{0, 0}, {128, 175}, {255, 255}};
  legacy_expected.green = {{0, 3}, {255, 252}};
  CHECK(patchy::acv::read(legacy.bytes()) == legacy_expected);

  // The high word is part of Photoshop's u32 bitmap, not a legacy u16 bitmap.
  // Channel 18 is validated and ignored by Patchy's RGB-only model.
  patchy::psd::BigEndianWriter high_channel;
  high_channel.write_u16(1);
  high_channel.write_u32(0x00040001U);  // Composite and channel 18.
  add_curve(high_channel, {{0, 7}, {255, 248}});
  add_curve(high_channel, {{0, 30}, {255, 220}});
  patchy::CurvesAdjustment high_channel_expected;
  high_channel_expected.rgb = {{0, 7}, {255, 248}};
  CHECK(patchy::acv::read(high_channel.bytes()) == high_channel_expected);

  // A zero bitmap is valid when all curves live in the indexed extension.
  patchy::psd::BigEndianWriter extension_only;
  extension_only.write_u16(1);
  extension_only.write_u32(0);
  extension_only.write_bytes(std::array<std::uint8_t, 4>{'C', 'r', 'v', ' '});
  extension_only.write_u16(4);
  extension_only.write_u32(1);
  extension_only.write_u16(0);
  add_curve(extension_only, {{0, 8}, {255, 247}});
  patchy::CurvesAdjustment extension_expected;
  extension_expected.rgb = {{0, 8}, {255, 247}};
  CHECK(patchy::acv::read(extension_only.bytes()) == extension_expected);

  // Exact bodies captured from Photoshop 2026 native `curv` blocks after
  // removing the PSD-only one-byte prefix. They pin the current u32 bitmap,
  // repeated indexed records, output/input order, zero-count identity, and
  // body padding.
  const std::vector<std::uint8_t> photoshop_identity{
      0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 'C',  'r',  'v',  ' ',  0x00, 0x04, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  CHECK(patchy::acv::read(photoshop_identity) == patchy::CurvesAdjustment{});

  const std::vector<std::uint8_t> photoshop_rich{
      0x00, 0x01, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00, 0x23,
      0x00, 0x40, 0x00, 0xbe, 0x00, 0x80, 0x00, 0xf5, 0x00, 0xff, 0x00, 0x04, 0x00, 0x14,
      0x00, 0x00, 0x00, 0x2d, 0x00, 0x50, 0x00, 0xe1, 0x00, 0xa0, 0x00, 0xe6, 0x00, 0xff,
      0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00, 0x78, 0x00, 0x60, 0x00, 0xee, 0x00, 0xdc,
      0x00, 0xfa, 0x00, 0xff, 0x00, 0x03, 0x00, 0xfa, 0x00, 0x00, 0x00, 0xa0, 0x00, 0x70,
      0x00, 0x05, 0x00, 0xff, 'C',  'r',  'v',  ' ',  0x00, 0x04, 0x00, 0x00, 0x00, 0x04,
      0x00, 0x00, 0x00, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00, 0x23, 0x00, 0x40, 0x00, 0xbe,
      0x00, 0x80, 0x00, 0xf5, 0x00, 0xff, 0x00, 0x01, 0x00, 0x04, 0x00, 0x14, 0x00, 0x00,
      0x00, 0x2d, 0x00, 0x50, 0x00, 0xe1, 0x00, 0xa0, 0x00, 0xe6, 0x00, 0xff, 0x00, 0x02,
      0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00, 0x78, 0x00, 0x60, 0x00, 0xee, 0x00, 0xdc,
      0x00, 0xfa, 0x00, 0xff, 0x00, 0x03, 0x00, 0x03, 0x00, 0xfa, 0x00, 0x00, 0x00, 0xa0,
      0x00, 0x70, 0x00, 0x05, 0x00, 0xff, 0x00, 0x00, 0x00};
  patchy::CurvesAdjustment photoshop_rich_expected;
  photoshop_rich_expected.rgb = {{0, 8}, {64, 35}, {128, 190}, {255, 245}};
  photoshop_rich_expected.red = {{0, 20}, {80, 45}, {160, 225}, {255, 230}};
  photoshop_rich_expected.green = {{0, 4}, {96, 120}, {220, 238}, {255, 250}};
  photoshop_rich_expected.blue = {{0, 250}, {112, 160}, {255, 5}};
  CHECK(patchy::acv::read(photoshop_rich) == photoshop_rich_expected);
}

void acv_rejects_malformed_data_and_invalid_models() {
  const auto read_throws = [](std::span<const std::uint8_t> bytes) {
    try {
      (void)patchy::acv::read(bytes);
      return false;
    } catch (const std::runtime_error&) {
      return true;
    }
  };

  CHECK(read_throws({}));
  CHECK(read_throws(std::array<std::uint8_t, 2>{0, 3}));
  CHECK(read_throws(std::array<std::uint8_t, 4>{0, 4, 0, 0}));
  CHECK(read_throws(std::array<std::uint8_t, 4>{0, 4, 0, 20}));

  patchy::psd::BigEndianWriter bad_point_count;
  bad_point_count.write_u16(4);
  bad_point_count.write_u16(1);
  bad_point_count.write_u16(1);
  bad_point_count.write_u16(0);
  bad_point_count.write_u16(0);
  CHECK(read_throws(bad_point_count.bytes()));

  patchy::psd::BigEndianWriter out_of_range;
  out_of_range.write_u16(4);
  out_of_range.write_u16(1);
  out_of_range.write_u16(2);
  out_of_range.write_u16(0);
  out_of_range.write_u16(0);
  out_of_range.write_u16(256);
  out_of_range.write_u16(255);
  CHECK(read_throws(out_of_range.bytes()));

  patchy::psd::BigEndianWriter duplicate_input;
  duplicate_input.write_u16(4);
  duplicate_input.write_u16(1);
  duplicate_input.write_u16(2);
  duplicate_input.write_u16(0);
  duplicate_input.write_u16(64);
  duplicate_input.write_u16(255);
  duplicate_input.write_u16(64);
  CHECK(read_throws(duplicate_input.bytes()));

  auto trailing = patchy::acv::write(patchy::CurvesAdjustment{});
  trailing.push_back(1);
  CHECK(read_throws(trailing));

  std::filesystem::create_directories("test-artifacts");
  const std::filesystem::path oversized_path = "test-artifacts/curves-oversized.acv";
  {
    std::ofstream file(oversized_path, std::ios::binary);
    const std::vector<char> oversized_bytes(4097, 0);
    file.write(oversized_bytes.data(), static_cast<std::streamsize>(oversized_bytes.size()));
    CHECK(static_cast<bool>(file));
  }
  bool oversized_file_threw = false;
  try {
    (void)patchy::acv::read_file(oversized_path);
  } catch (const std::runtime_error&) {
    oversized_file_threw = true;
  }
  CHECK(oversized_file_threw);

  patchy::CurvesAdjustment invalid;
  invalid.red = {{0, 0}};
  bool write_threw = false;
  try {
    (void)patchy::acv::write(invalid);
  } catch (const std::runtime_error&) {
    write_threw = true;
  }
  CHECK(write_threw);

  invalid = {};
  invalid.blue = {{0, 0}, {0, 255}};
  write_threw = false;
  try {
    (void)patchy::acv::write(invalid);
  } catch (const std::runtime_error&) {
    write_threw = true;
  }
  CHECK(write_threw);
}

void curves_lut_export_targets_match_reference_and_preserve_alpha() {
  patchy::Document document(4, 1, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(4, 1, patchy::PixelFormat::rgba8());
  constexpr std::array<patchy::RgbColor, 4> kColors{{{12, 34, 56}, {64, 96, 128}, {127, 128, 129}, {230, 180, 90}}};
  constexpr std::array<std::uint8_t, 4> kAlpha{{0, 64, 128, 255}};
  for (std::int32_t x = 0; x < 4; ++x) {
    auto* pixel = pixels.pixel(x, 0);
    const auto& color = kColors[static_cast<std::size_t>(x)];
    pixel[0] = color.red;
    pixel[1] = color.green;
    pixel[2] = color.blue;
    pixel[3] = kAlpha[static_cast<std::size_t>(x)];
  }
  document.add_pixel_layer("Source", std::move(pixels));
  const auto unadjusted_rgba = patchy::flatten_document_rgba8(document);

  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Curves;
  settings.curves.rgb = {{0, 8}, {64, 35}, {128, 190}, {255, 245}};
  settings.curves.red = {{0, 20}, {80, 45}, {160, 225}, {255, 230}};
  settings.curves.green = {{0, 0}, {96, 120}, {255, 255}};
  settings.curves.blue = {{0, 255}, {255, 0}};
  constexpr float kAmount = 0.625F;
  patchy::Layer adjustment(document.allocate_layer_id(), "Curves", patchy::LayerKind::Adjustment);
  adjustment.set_bounds(patchy::Rect::from_size(document.width(), document.height()));
  adjustment.set_opacity(kAmount);
  patchy::configure_adjustment_layer(adjustment, settings);
  document.add_layer(std::move(adjustment));

  const auto lut = patchy::build_curves_lut(settings.curves);
  const auto rgba_flattened = patchy::flatten_document_rgba8(document);
  for (std::int32_t x = 0; x < 4; ++x) {
    const auto* before = unadjusted_rgba.pixel(x, 0);
    const auto* after = rgba_flattened.pixel(x, 0);
    CHECK(after[3] == before[3]);
    if (before[3] == 0) {
      CHECK(after[0] == before[0]);
      CHECK(after[1] == before[1]);
      CHECK(after[2] == before[2]);
      continue;
    }
    CHECK(after[0] == patchy::clamp_byte(static_cast<float>(lut.red[before[0]]) * kAmount +
                                         static_cast<float>(before[0]) * (1.0F - kAmount)));
    CHECK(after[1] == patchy::clamp_byte(static_cast<float>(lut.green[before[1]]) * kAmount +
                                         static_cast<float>(before[1]) * (1.0F - kAmount)));
    CHECK(after[2] == patchy::clamp_byte(static_cast<float>(lut.blue[before[2]]) * kAmount +
                                         static_cast<float>(before[2]) * (1.0F - kAmount)));
  }

  const auto rgba_bytes = patchy::bmp::DocumentIo::write(
      document,
      patchy::bmp::WriteOptions{patchy::bmp::BmpEncoding::Rgba32, patchy::bmp::BmpPaletteMode::Exact, true});
  const auto rgba_read = patchy::bmp::DocumentIo::read(rgba_bytes);
  const auto rgba_read_data = rgba_read.layers().front().pixels().data();
  const auto rgba_flattened_data = rgba_flattened.data();
  CHECK(rgba_read_data.size() == rgba_flattened_data.size());
  CHECK(std::equal(rgba_read_data.begin(), rgba_read_data.end(), rgba_flattened_data.begin()));

  const auto rgb_bytes = patchy::bmp::DocumentIo::write(
      document,
      patchy::bmp::WriteOptions{patchy::bmp::BmpEncoding::Rgb24, patchy::bmp::BmpPaletteMode::Exact, true});
  const auto rgb_read = patchy::bmp::DocumentIo::read(rgb_bytes);
  const auto& rgb_pixels = rgb_read.layers().front().pixels();
  for (std::int32_t x = 0; x < 4; ++x) {
    const auto& color = kColors[static_cast<std::size_t>(x)];
    const std::array<std::uint8_t, 3> source{color.red, color.green, color.blue};
    const std::array<std::uint8_t, 3> white{255, 255, 255};
    const auto on_white = patchy::composite_blended_rgb(
        source, white, patchy::BlendMode::Normal,
        static_cast<float>(kAlpha[static_cast<std::size_t>(x)]) / 255.0F, 1.0F);
    const auto* actual = rgb_pixels.pixel(x, 0);
    CHECK(actual[0] == patchy::clamp_byte(static_cast<float>(lut.red[on_white[0]]) * kAmount +
                                          static_cast<float>(on_white[0]) * (1.0F - kAmount)));
    CHECK(actual[1] == patchy::clamp_byte(static_cast<float>(lut.green[on_white[1]]) * kAmount +
                                          static_cast<float>(on_white[1]) * (1.0F - kAmount)));
    CHECK(actual[2] == patchy::clamp_byte(static_cast<float>(lut.blue[on_white[2]]) * kAmount +
                                          static_cast<float>(on_white[2]) * (1.0F - kAmount)));
  }
}

void curves_metadata_prefers_rich_points_and_keeps_legacy_fallback() {
  patchy::Layer legacy(1, "Legacy Curves", patchy::LayerKind::Adjustment);
  legacy.metadata()[patchy::kLayerMetadataAdjustmentType] = "curves";
  legacy.metadata()[patchy::kLayerMetadataAdjustmentCurvesShadowOutput] = "14";
  legacy.metadata()[patchy::kLayerMetadataAdjustmentCurvesMidtoneOutput] = "171";
  legacy.metadata()[patchy::kLayerMetadataAdjustmentCurvesHighlightOutput] = "241";
  const auto legacy_settings = patchy::adjustment_settings_from_layer(legacy);
  CHECK(legacy_settings.has_value());
  CHECK(legacy_settings->curves.rgb ==
        patchy::CurveControlPoints({{0, 14}, {128, 171}, {255, 241}}));
  CHECK(legacy_settings->curves.red == patchy::CurveControlPoints({{0, 0}, {255, 255}}));

  patchy::AdjustmentSettings rich;
  rich.kind = patchy::AdjustmentKind::Curves;
  rich.curves.rgb = {{9, 9}, {72, 44}, {128, 188}, {247, 247}};
  rich.curves.red = {{4, 0}, {110, 148}, {250, 255}};
  rich.curves.green = {{8, 4}, {246, 251}};
  rich.curves.blue = {{6, 18}, {180, 160}, {244, 238}};
  patchy::Layer layer(2, "Rich Curves", patchy::LayerKind::Adjustment);
  patchy::configure_adjustment_layer(layer, rich);
  const auto round_tripped = patchy::adjustment_settings_from_layer(std::as_const(layer));
  CHECK(round_tripped.has_value());
  CHECK(round_tripped->curves == rich.curves);
  const auto master = patchy::build_curve_lut(rich.curves.rgb);
  const auto& metadata = std::as_const(layer).metadata();
  CHECK(metadata.at(patchy::kLayerMetadataAdjustmentCurvesShadowOutput) == std::to_string(master[0]));
  CHECK(metadata.at(patchy::kLayerMetadataAdjustmentCurvesMidtoneOutput) == std::to_string(master[128]));
  CHECK(metadata.at(patchy::kLayerMetadataAdjustmentCurvesHighlightOutput) == std::to_string(master[255]));
  CHECK(metadata.at(patchy::kLayerMetadataAdjustmentCurvesRgbPoints) == "9:9;72:44;128:188;247:247");
}

std::vector<std::uint8_t> test_patchy_curves_plad_payload(const patchy::CurvesAdjustment& curves,
                                                          bool include_rich_tail) {
  patchy::psd::BigEndianWriter writer;
  writer.write_bytes(std::array<std::uint8_t, 4>{'P', 'L', 'A', 'D'});
  writer.write_u16(4);
  writer.write_u8(1);  // Curves.
  const auto write_i32 = [&writer](int value) { writer.write_u32(static_cast<std::uint32_t>(value)); };
  for (int channel = 0; channel < 4; ++channel) {
    write_i32(0);
    write_i32(255);
    write_i32(100);
    write_i32(0);
    write_i32(255);
  }
  write_i32(0);  // Levels channel.
  const auto composite = patchy::build_curve_lut(curves.rgb);
  write_i32(composite[0]);
  write_i32(composite[128]);
  write_i32(composite[255]);
  for (int unused_adjustment_value = 0; unused_adjustment_value < 6; ++unused_adjustment_value) {
    write_i32(0);
  }
  write_i32(0);   // Hue/Saturation colorize disabled.
  write_i32(0);   // Colorize hue.
  write_i32(25);  // Colorize saturation default.
  write_i32(0);   // Colorize lightness.

  if (include_rich_tail) {
    patchy::psd::BigEndianWriter extension;
    extension.write_u16(1);
    extension.write_u16(4);
    constexpr std::array channels{patchy::CurvesChannel::Rgb, patchy::CurvesChannel::Red,
                                  patchy::CurvesChannel::Green, patchy::CurvesChannel::Blue};
    for (std::size_t channel_index = 0; channel_index < channels.size(); ++channel_index) {
      const auto points =
          patchy::normalized_curve_control_points(patchy::curve_points_for_channel(curves, channels[channel_index]));
      extension.write_u8(static_cast<std::uint8_t>(channel_index));
      extension.write_u8(0);
      extension.write_u16(static_cast<std::uint16_t>(points.size()));
      for (const auto& point : points) {
        extension.write_u16(static_cast<std::uint16_t>(point.input));
        extension.write_u16(static_cast<std::uint16_t>(point.output));
      }
    }
    writer.write_bytes(std::array<std::uint8_t, 4>{'C', 'R', 'V', '2'});
    writer.write_u32(static_cast<std::uint32_t>(extension.bytes().size()));
    writer.write_bytes(extension.bytes());
  }
  return writer.bytes();
}

void psd_curves_native_output_omits_private_plad_and_migrates_legacy() {
  const auto write_document = [](const patchy::CurvesAdjustment& curves) {
    patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(1, 1, 80, 100, 120));
    patchy::AdjustmentSettings settings;
    settings.kind = patchy::AdjustmentKind::Curves;
    settings.curves = curves;
    patchy::Layer adjustment(document.allocate_layer_id(), "Curves", patchy::LayerKind::Adjustment);
    adjustment.set_bounds(patchy::Rect::from_size(1, 1));
    patchy::configure_adjustment_layer(adjustment, settings);
    document.add_layer(std::move(adjustment));
    return patchy::psd::DocumentIo::write_layered_rgb8(document);
  };
  const auto identity_bytes = write_document(patchy::CurvesAdjustment{});
  const auto identity_extra = psd_layer_extra_data(identity_bytes, 1);
  CHECK(!psd_layer_block_payload(identity_extra, "plAD").has_value());
  const auto identity_native = psd_layer_block_payload(identity_extra, "curv");
  CHECK(identity_native.has_value());
  const std::vector<std::uint8_t> expected_identity_native{
      0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 'C', 'r', 'v', ' ',
      0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  CHECK(*identity_native == expected_identity_native);

  const auto legacy_curves = patchy::curves_adjustment_from_legacy_outputs(17, 184, 243);
  const auto legacy_bytes = write_document(legacy_curves);
  const auto legacy_extra = psd_layer_extra_data(legacy_bytes, 1);
  CHECK(!psd_layer_block_payload(legacy_extra, "plAD").has_value());
  CHECK(psd_layer_block_payload(legacy_extra, "curv").has_value());

  const auto private_payload = test_patchy_curves_plad_payload(legacy_curves, false);
  const auto private_bytes = single_adjustment_layer_psd({{{'p', 'l', 'A', 'D'}, private_payload}});
  const auto private_document = patchy::psd::DocumentIo::read(private_bytes);
  CHECK(private_document.layers().size() == 1U);
  const auto private_settings = patchy::adjustment_settings_from_layer(private_document.layers().front());
  CHECK(private_settings.has_value());
  CHECK(private_settings->curves == legacy_curves);

  const auto migrated = patchy::psd::DocumentIo::write_layered_rgb8(private_document);
  const auto migrated_extra = psd_layer_extra_data(migrated, 0);
  CHECK(!psd_layer_block_payload(migrated_extra, "plAD").has_value());
  CHECK(psd_layer_block_payload(migrated_extra, "curv").has_value());
  const auto migrated_document = patchy::psd::DocumentIo::read(migrated);
  const auto migrated_settings = patchy::adjustment_settings_from_layer(migrated_document.layers().front());
  CHECK(migrated_settings.has_value());
  CHECK(migrated_settings->curves == legacy_curves);
}

void psd_curves_private_plad_v4_imports_and_malformed_tail_migrates_fallback() {
  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Curves;
  settings.curves.rgb = {{12, 6}, {48, 24}, {128, 201}, {220, 214}, {248, 249}};
  settings.curves.red = {{5, 0}, {100, 140}, {252, 255}};
  settings.curves.green = {{8, 12}, {247, 244}};
  settings.curves.blue = {{3, 255}, {96, 180}, {249, 0}};
  const auto payload = test_patchy_curves_plad_payload(settings.curves, true);
  constexpr std::size_t kCurvesTailOffset = 4U + 2U + 1U + 30U * 4U + 4U * 4U;
  CHECK(payload.size() > kCurvesTailOffset + 8U);
  CHECK(payload[4] == 0U);
  CHECK(payload[5] == 4U);
  CHECK(std::string(payload.begin() + static_cast<std::ptrdiff_t>(kCurvesTailOffset),
                    payload.begin() + static_cast<std::ptrdiff_t>(kCurvesTailOffset + 4U)) == "CRV2");

  const auto read = patchy::psd::DocumentIo::read(
      single_adjustment_layer_psd({{{'p', 'l', 'A', 'D'}, payload}}));
  const auto rich_settings = patchy::adjustment_settings_from_layer(read.layers().front());
  CHECK(rich_settings.has_value());
  CHECK(rich_settings->kind == patchy::AdjustmentKind::Curves);
  CHECK(rich_settings->curves == settings.curves);
  const auto rich_migrated = patchy::psd::DocumentIo::write_layered_rgb8(read);
  const auto rich_migrated_extra = psd_layer_extra_data(rich_migrated, 0);
  CHECK(!psd_layer_block_payload(rich_migrated_extra, "plAD").has_value());
  CHECK(psd_layer_block_payload(rich_migrated_extra, "curv").has_value());
  const auto rich_round_trip = patchy::psd::DocumentIo::read(rich_migrated);
  const auto rich_round_trip_settings =
      patchy::adjustment_settings_from_layer(rich_round_trip.layers().front());
  CHECK(rich_round_trip_settings.has_value());
  CHECK(rich_round_trip_settings->curves == settings.curves);

  const auto master = patchy::build_curve_lut(settings.curves.rgb);
  const auto expected_fallback =
      patchy::curves_adjustment_from_legacy_outputs(master[0], master[128], master[255]);
  const auto check_fallback = [&expected_fallback](const std::vector<std::uint8_t>& candidate) {
    const auto candidate_psd = single_adjustment_layer_psd({{{'p', 'l', 'A', 'D'}, candidate}});
    const auto fallback_document = patchy::psd::DocumentIo::read(candidate_psd);
    CHECK(fallback_document.layers().size() == 1U);
    const auto fallback_settings = patchy::adjustment_settings_from_layer(fallback_document.layers().front());
    CHECK(fallback_settings.has_value());
    CHECK(fallback_settings->kind == patchy::AdjustmentKind::Curves);
    CHECK(fallback_settings->curves == expected_fallback);
    const auto rewritten = patchy::psd::DocumentIo::write_layered_rgb8(fallback_document);
    const auto rewritten_extra = psd_layer_extra_data(rewritten, 0);
    CHECK(!psd_layer_block_payload(rewritten_extra, "plAD").has_value());
    CHECK(psd_layer_block_payload(rewritten_extra, "curv").has_value());
    const auto migrated_document = patchy::psd::DocumentIo::read(rewritten);
    const auto migrated_settings =
        patchy::adjustment_settings_from_layer(migrated_document.layers().front());
    CHECK(migrated_settings.has_value());
    CHECK(migrated_settings->curves == expected_fallback);
  };

  auto malformed = payload;
  malformed[kCurvesTailOffset + 4U] = 0x7fU;
  malformed[kCurvesTailOffset + 5U] = 0xffU;
  malformed[kCurvesTailOffset + 6U] = 0xffU;
  malformed[kCurvesTailOffset + 7U] = 0xffU;
  check_fallback(malformed);

  const auto write_u32_at = [](std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 24U);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 16U);
    bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value);
  };
  auto truncated = payload;
  truncated.resize(truncated.size() - 2U);
  write_u32_at(truncated, kCurvesTailOffset + 4U,
               static_cast<std::uint32_t>(truncated.size() - kCurvesTailOffset - 8U));
  check_fallback(truncated);

  constexpr std::size_t kExtensionBodyOffset = kCurvesTailOffset + 8U;
  constexpr std::size_t kFirstChannelIdOffset = kExtensionBodyOffset + 4U;
  auto invalid_channel = payload;
  invalid_channel[kFirstChannelIdOffset] = 9U;
  check_fallback(invalid_channel);

  const auto first_channel_count =
      (static_cast<std::size_t>(payload[kFirstChannelIdOffset + 2U]) << 8U) |
      static_cast<std::size_t>(payload[kFirstChannelIdOffset + 3U]);
  const auto second_channel_id_offset = kFirstChannelIdOffset + 4U + first_channel_count * 4U;
  auto duplicate_channel = payload;
  duplicate_channel[second_channel_id_offset] = duplicate_channel[kFirstChannelIdOffset];
  check_fallback(duplicate_channel);

  auto future_version = payload;
  future_version[kExtensionBodyOffset] = 0U;
  future_version[kExtensionBodyOffset + 1U] = 2U;
  check_fallback(future_version);

  // The v1 shape can never exceed 324 bytes (four 19-point channels). Reject
  // larger declared records before copying them, even when the enclosing plAD
  // really contains all of those bytes. Migration keeps the modeled fallback.
  auto oversized = std::vector<std::uint8_t>(
      payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(kCurvesTailOffset));
  oversized.insert(oversized.end(), {'C', 'R', 'V', '2'});
  oversized.resize(kCurvesTailOffset + 8U + 325U, 0xa5U);
  write_u32_at(oversized, kCurvesTailOffset + 4U, 325U);
  check_fallback(oversized);

  // Once Patchy changes the modeled curve, the opaque future record must no
  // longer shadow the edit: the modeled fallback plus edit migrate to curv.
  auto edited_document = patchy::psd::DocumentIo::read(
      single_adjustment_layer_psd({{{'p', 'l', 'A', 'D'}, future_version}}));
  auto edited_settings = patchy::adjustment_settings_from_layer(edited_document.layers().front());
  CHECK(edited_settings.has_value());
  edited_settings->curves.red = {{0, 5}, {96, 142}, {255, 250}};
  patchy::configure_adjustment_layer(edited_document.layers().front(), *edited_settings);
  const auto edited_bytes = patchy::psd::DocumentIo::write_layered_rgb8(edited_document);
  const auto edited_extra = psd_layer_extra_data(edited_bytes, 0);
  CHECK(!psd_layer_block_payload(edited_extra, "plAD").has_value());
  CHECK(psd_layer_block_payload(edited_extra, "curv").has_value());
  const auto edited_round_trip = patchy::psd::DocumentIo::read(edited_bytes);
  const auto edited_round_trip_settings =
      patchy::adjustment_settings_from_layer(edited_round_trip.layers().front());
  CHECK(edited_round_trip_settings.has_value());
  CHECK(edited_round_trip_settings->curves == edited_settings->curves);
}

void psd_native_curves_suppresses_private_fallback_and_preserves_both_blocks() {
  patchy::AdjustmentSettings stale_settings;
  stale_settings.kind = patchy::AdjustmentKind::Curves;
  stale_settings.curves.rgb = {{0, 0}, {128, 220}, {255, 255}};
  const auto stale_plad = test_patchy_curves_plad_payload(stale_settings.curves, false);

  const std::vector<std::uint8_t> native_curv{0x00, 0x01, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78};
  const auto bytes = single_adjustment_layer_psd(
      {{{'c', 'u', 'r', 'v'}, native_curv}, {{'p', 'l', 'A', 'D'}, stale_plad}});
  const auto document = patchy::psd::DocumentIo::read(bytes);
  CHECK(document.layers().size() == 1U);
  CHECK(document.layers().front().kind() == patchy::LayerKind::Pixel);
  CHECK(!patchy::adjustment_settings_from_layer(document.layers().front()).has_value());

  const auto rewritten = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto rewritten_extra = psd_layer_extra_data(rewritten, 0);
  const auto rewritten_curv = psd_layer_block_payload(rewritten_extra, "curv");
  const auto rewritten_plad = psd_layer_block_payload(rewritten_extra, "plAD");
  CHECK(rewritten_curv.has_value());
  CHECK(*rewritten_curv == native_curv);
  CHECK(rewritten_plad.has_value());
  CHECK(*rewritten_plad == stale_plad);

  patchy::Document native_document(1, 1, patchy::PixelFormat::rgb8());
  native_document.add_pixel_layer("Base", solid_rgb(1, 1, 40, 80, 120));
  patchy::AdjustmentSettings native_settings;
  native_settings.kind = patchy::AdjustmentKind::Curves;
  native_settings.curves.rgb = {{0, 4}, {128, 180}, {255, 250}};
  native_settings.curves.red = {{0, 12}, {255, 242}};
  patchy::Layer native_adjustment(native_document.allocate_layer_id(), "Native Curves",
                                  patchy::LayerKind::Adjustment);
  native_adjustment.set_bounds(patchy::Rect::from_size(1, 1));
  patchy::configure_adjustment_layer(native_adjustment, native_settings);
  native_document.add_layer(std::move(native_adjustment));
  const auto native_extra =
      psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(native_document), 1);
  const auto valid_native_curv = psd_layer_block_payload(native_extra, "curv");
  CHECK(valid_native_curv.has_value());

  const auto conflicting_bytes = single_adjustment_layer_psd(
      {{{'c', 'u', 'r', 'v'}, *valid_native_curv}, {{'p', 'l', 'A', 'D'}, stale_plad}});
  const auto conflicting_document = patchy::psd::DocumentIo::read(conflicting_bytes);
  CHECK(conflicting_document.layers().front().kind() == patchy::LayerKind::Adjustment);
  const auto conflicting_settings =
      patchy::adjustment_settings_from_layer(conflicting_document.layers().front());
  CHECK(conflicting_settings.has_value());
  CHECK(conflicting_settings->curves == native_settings.curves);
}

void psd_photoshop_curves_fixtures_import_preserve_regenerate_and_round_trip() {
  patchy::CurvesAdjustment expected;
  expected.rgb = {{0, 8}, {64, 35}, {128, 190}, {255, 245}};
  expected.red = {{0, 20}, {80, 45}, {160, 225}, {255, 230}};
  expected.green = {{0, 4}, {96, 120}, {220, 238}, {255, 250}};
  expected.blue = {{0, 250}, {112, 160}, {255, 5}};

  struct FixtureCase {
    const char* filename;
    const char* layer_name;
    bool clipped;
    bool patterned_mask;
  };
  constexpr std::array<FixtureCase, 2> kCases{{
      {"photoshop-curves-masked.psd", "Curves rich-masked", false, true},
      {"photoshop-curves-clipped.psd", "Curves rich-clipped", true, false},
  }};

  const auto block_payload = [](const patchy::Layer& layer,
                                std::string_view key) -> const std::vector<std::uint8_t>* {
    const auto& blocks = layer.unknown_psd_blocks();
    const auto found = std::find_if(blocks.begin(), blocks.end(), [key](const patchy::UnknownPsdBlock& block) {
      return block.key == key;
    });
    return found == blocks.end() ? nullptr : &found->payload;
  };

  const auto check_mask = [](const patchy::Layer& layer, bool patterned) {
    if (!patterned) {
      CHECK(!layer.mask().has_value());
      return;
    }
    CHECK(layer.mask().has_value());
    const auto& mask = *layer.mask();
    CHECK(mask.bounds.x == 0);
    CHECK(mask.bounds.y == 0);
    CHECK(mask.bounds.width == 4);
    CHECK(mask.bounds.height == 8);
    CHECK(mask.default_color == 255);
    for (std::int32_t y = 0; y < 8; ++y) {
      for (std::int32_t x = 0; x < 4; ++x) {
        CHECK(mask.pixels.pixel(x, y)[0] == 0);
      }
    }
  };

  patchy::Document fresh_document(8, 8, patchy::PixelFormat::rgb8());
  fresh_document.add_pixel_layer("Base", solid_rgb(8, 8, 50, 100, 150));
  patchy::AdjustmentSettings fresh_settings;
  fresh_settings.kind = patchy::AdjustmentKind::Curves;
  fresh_settings.curves = expected;
  patchy::Layer fresh_adjustment(fresh_document.allocate_layer_id(), "Fresh Curves",
                                 patchy::LayerKind::Adjustment);
  fresh_adjustment.set_bounds(patchy::Rect::from_size(8, 8));
  patchy::configure_adjustment_layer(fresh_adjustment, fresh_settings);
  fresh_document.add_layer(std::move(fresh_adjustment));
  const auto fresh_curv = psd_layer_block_payload(
      psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(fresh_document), 1), "curv");
  CHECK(fresh_curv.has_value());
  CHECK(fresh_curv->size() == 164U);

  for (const auto& fixture : kCases) {
    const auto path = patchy::test::committed_psd_fixture_path(fixture.filename);
    CHECK(std::filesystem::exists(path));
    auto document = patchy::psd::DocumentIo::read_file(path);
    CHECK(document.width() == 8);
    CHECK(document.height() == 8);
    CHECK(document.layers().size() == 2U);

    const auto* adjustment = find_layer_named(std::as_const(document).layers(), fixture.layer_name);
    CHECK(adjustment != nullptr);
    CHECK(adjustment->kind() == patchy::LayerKind::Adjustment);
    CHECK(adjustment->clipped() == fixture.clipped);
    check_mask(*adjustment, fixture.patterned_mask);
    const auto settings = patchy::adjustment_settings_from_layer(*adjustment);
    CHECK(settings.has_value());
    CHECK(settings->kind == patchy::AdjustmentKind::Curves);
    CHECK(settings->curves == expected);

    const auto* original_curv = block_payload(*adjustment, "curv");
    CHECK(original_curv != nullptr);
    CHECK(original_curv->size() == 164U);
    const auto original_curv_copy = *original_curv;
    CHECK(original_curv_copy == *fresh_curv);

    const auto rewritten_bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    const auto round_tripped = patchy::psd::DocumentIo::read(rewritten_bytes);
    const auto* round_tripped_adjustment =
        find_layer_named(round_tripped.layers(), fixture.layer_name);
    CHECK(round_tripped_adjustment != nullptr);
    CHECK(round_tripped_adjustment->clipped() == fixture.clipped);
    check_mask(*round_tripped_adjustment, fixture.patterned_mask);
    const auto round_tripped_settings = patchy::adjustment_settings_from_layer(*round_tripped_adjustment);
    CHECK(round_tripped_settings.has_value());
    CHECK(round_tripped_settings->curves == expected);
    const auto* round_tripped_curv = block_payload(*round_tripped_adjustment, "curv");
    CHECK(round_tripped_curv != nullptr);
    CHECK(*round_tripped_curv == original_curv_copy);

    auto edited = *settings;
    edited.curves.red[1].output = 52;
    auto* mutable_adjustment = document.find_layer(adjustment->id());
    CHECK(mutable_adjustment != nullptr);
    patchy::configure_adjustment_layer(*mutable_adjustment, edited);

    const auto edited_bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    const auto edited_round_trip = patchy::psd::DocumentIo::read(edited_bytes);
    const auto* edited_adjustment = find_layer_named(edited_round_trip.layers(), fixture.layer_name);
    CHECK(edited_adjustment != nullptr);
    CHECK(edited_adjustment->clipped() == fixture.clipped);
    check_mask(*edited_adjustment, fixture.patterned_mask);
    const auto edited_settings = patchy::adjustment_settings_from_layer(*edited_adjustment);
    CHECK(edited_settings.has_value());
    CHECK(edited_settings->curves == edited.curves);
    const auto* edited_curv = block_payload(*edited_adjustment, "curv");
    CHECK(edited_curv != nullptr);
    CHECK(edited_curv->size() == 164U);
    CHECK(*edited_curv != original_curv_copy);
  }
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

// Photoshop's hue2 payload: 16-byte header + six hextant band records + the
// 36-byte trailer PS always writes (see docs/ps-compat.md).
std::vector<std::uint8_t> test_hue2_payload(bool colorize, int colorize_hue, int colorize_saturation,
                                            int colorize_lightness, int master_hue, int master_saturation,
                                            int master_lightness, bool include_trailer = true,
                                            int first_band_setting = 0) {
  patchy::psd::BigEndianWriter writer;
  const auto write_i16 = [&writer](int value) {
    writer.write_u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(value)));
  };
  writer.write_u16(2);
  writer.write_u8(colorize ? 1 : 0);
  writer.write_u8(0);
  write_i16(colorize_hue);
  write_i16(colorize_saturation);
  write_i16(colorize_lightness);
  write_i16(master_hue);
  write_i16(master_saturation);
  write_i16(master_lightness);
  constexpr std::array<std::array<int, 4>, 6> band_ranges{{{315, 345, 15, 45},
                                                           {15, 45, 75, 105},
                                                           {75, 105, 135, 165},
                                                           {135, 165, 195, 225},
                                                           {195, 225, 255, 285},
                                                           {255, 285, 315, 345}}};
  for (std::size_t band = 0; band < band_ranges.size(); ++band) {
    for (const auto value : band_ranges[band]) {
      write_i16(value);
    }
    write_i16(band == 0 ? first_band_setting : 0);
    write_i16(0);
    write_i16(0);
  }
  if (include_trailer) {
    for (int k = 0; k < 6; ++k) {
      write_i16(k * 60);
      write_i16(100);
      write_i16(50);
    }
  }
  return writer.bytes();
}

void adjustment_hue_saturation_colorize_matches_photoshop_reference() {
  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::HueSaturation;
  settings.hue_saturation.colorize = true;
  settings.hue_saturation.colorize_hue = 203;
  settings.hue_saturation.colorize_saturation = 52;
  settings.hue_saturation.colorize_lightness = 0;
  // Stale master values must be ignored while colorize is active (PS-verified).
  settings.hue_saturation.hue_shift = -180;
  settings.hue_saturation.saturation_delta = -61;

  CHECK(patchy::adjustment_has_effect(settings));

  const auto check_color = [&settings](patchy::RgbColor input, patchy::RgbColor expected) {
    const auto actual = patchy::apply_adjustment_to_color(input, settings);
    CHECK(actual.red == expected.red);
    CHECK(actual.green == expected.green);
    CHECK(actual.blue == expected.blue);
  };
  // Pinned against Photoshop 2026 renders (docs/ps-compat.md calibration).
  check_color({0, 0, 0}, {0, 0, 0});
  check_color({64, 64, 64}, {31, 71, 97});
  check_color({120, 120, 120}, {58, 132, 182});
  check_color({128, 128, 128}, {62, 141, 194});
  check_color({200, 200, 200}, {172, 206, 229});
  check_color({255, 255, 255}, {255, 255, 255});
  check_color({220, 140, 60}, {81, 152, 200});

  settings.hue_saturation.colorize_hue = 204;
  settings.hue_saturation.colorize_lightness = 40;
  check_color({100, 100, 100}, {114, 170, 210});
  settings.hue_saturation.colorize_lightness = -40;
  check_color({100, 100, 100}, {29, 65, 91});
  settings.hue_saturation.colorize_lightness = 0;

  settings.hue_saturation.colorize_hue = 0;
  settings.hue_saturation.colorize_saturation = 25;
  check_color({128, 128, 128}, {160, 97, 97});
  settings.hue_saturation.colorize_hue = 120;
  settings.hue_saturation.colorize_saturation = 100;
  check_color({128, 128, 128}, {1, 255, 1});
  settings.hue_saturation.colorize_hue = 300;
  settings.hue_saturation.colorize_saturation = 52;
  check_color({90, 90, 90}, {137, 44, 137});

  // Colorize-only settings survive the metadata round-trip.
  patchy::Layer layer(1, "Colorize", patchy::LayerKind::Adjustment);
  settings.hue_saturation.colorize_hue = 203;
  patchy::configure_adjustment_layer(layer, settings);
  const auto round_tripped = patchy::adjustment_settings_from_layer(layer);
  CHECK(round_tripped.has_value());
  CHECK(round_tripped->hue_saturation.colorize);
  CHECK(round_tripped->hue_saturation.colorize_hue == 203);
  CHECK(round_tripped->hue_saturation.colorize_saturation == 52);
  CHECK(round_tripped->hue_saturation.colorize_lightness == 0);
  CHECK(round_tripped->hue_saturation.hue_shift == -180);

  // The identity master settings with colorize OFF have no effect.
  patchy::AdjustmentSettings identity;
  identity.kind = patchy::AdjustmentKind::HueSaturation;
  CHECK(!patchy::adjustment_has_effect(identity));
  identity.hue_saturation.colorize = true;
  CHECK(patchy::adjustment_has_effect(identity));
}

void psd_native_hue2_colorize_adjustment_imports_and_renders() {
  // The exact header generic_bg.psd carries: colorize on, hue -157 (= 203),
  // saturation 52, stale master (-180, -61, 0).
  const auto bytes =
      single_adjustment_layer_psd({{{'h', 'u', 'e', '2'}, test_hue2_payload(true, -157, 52, 0, -180, -61, 0)}});
  const auto document = patchy::psd::DocumentIo::read(bytes);
  CHECK(document.layers().size() == 1);
  CHECK(document.layers().front().kind() == patchy::LayerKind::Adjustment);
  const auto settings = patchy::adjustment_settings_from_layer(document.layers().front());
  CHECK(settings.has_value());
  CHECK(settings->kind == patchy::AdjustmentKind::HueSaturation);
  CHECK(settings->hue_saturation.colorize);
  CHECK(settings->hue_saturation.colorize_hue == 203);
  CHECK(settings->hue_saturation.colorize_saturation == 52);
  CHECK(settings->hue_saturation.colorize_lightness == 0);
  CHECK(settings->hue_saturation.hue_shift == -180);
  CHECK(settings->hue_saturation.saturation_delta == -61);

  patchy::Document composited(1, 1, patchy::PixelFormat::rgb8());
  composited.add_pixel_layer("Base", solid_rgb(1, 1, 120, 120, 120));
  patchy::Layer adjustment(composited.allocate_layer_id(), "Native Colorize", patchy::LayerKind::Adjustment);
  patchy::configure_adjustment_layer(adjustment, *settings);
  composited.add_layer(std::move(adjustment));
  const auto flattened = patchy::Compositor{}.flatten_rgb8(composited);
  CHECK(flattened.pixel(0, 0)[0] == 58);
  CHECK(flattened.pixel(0, 0)[1] == 132);
  CHECK(flattened.pixel(0, 0)[2] == 182);
}

void psd_hue_saturation_adjustment_writes_native_hue2_and_patchy_fallback() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(1, 1, 120, 120, 120));
  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::HueSaturation;
  settings.hue_saturation.colorize = true;
  settings.hue_saturation.colorize_hue = 203;
  settings.hue_saturation.colorize_saturation = 52;
  patchy::Layer adjustment(document.allocate_layer_id(), "Colorize", patchy::LayerKind::Adjustment);
  adjustment.set_bounds(patchy::Rect::from_size(1, 1));
  patchy::configure_adjustment_layer(adjustment, settings);
  document.add_layer(std::move(adjustment));

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto extra = psd_layer_extra_data(bytes, 1);
  const auto hue2 = psd_layer_block_payload(extra, "hue2");
  CHECK(hue2.has_value());
  // A Patchy-authored layer emits the byte-exact fresh-layer template PS writes.
  CHECK(*hue2 == test_hue2_payload(true, -157, 52, 0, 0, 0, 0));
  const auto plad = psd_layer_block_payload(extra, "plAD");
  CHECK(plad.has_value());
  CHECK(plad->size() == 143);  // 127-byte version 4 body + 16-byte colorize extension

  const auto read = patchy::psd::DocumentIo::read(bytes);
  const auto round_tripped = patchy::adjustment_settings_from_layer(read.layers().back());
  CHECK(round_tripped.has_value());
  CHECK(round_tripped->hue_saturation.colorize);
  CHECK(round_tripped->hue_saturation.colorize_hue == 203);
  CHECK(round_tripped->hue_saturation.colorize_saturation == 52);
}

void psd_native_hue2_overrides_stale_patchy_fallback() {
  patchy::Document stale_document(1, 1, patchy::PixelFormat::rgb8());
  stale_document.add_pixel_layer("Base", solid_rgb(1, 1, 120, 120, 120));
  patchy::AdjustmentSettings stale_settings;
  stale_settings.kind = patchy::AdjustmentKind::HueSaturation;
  stale_settings.hue_saturation.hue_shift = 90;
  patchy::Layer stale_adjustment(stale_document.allocate_layer_id(), "Stale", patchy::LayerKind::Adjustment);
  stale_adjustment.set_bounds(patchy::Rect::from_size(1, 1));
  patchy::configure_adjustment_layer(stale_adjustment, stale_settings);
  stale_document.add_layer(std::move(stale_adjustment));
  const auto stale_extra = psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(stale_document), 1);
  const auto stale_plad = psd_layer_block_payload(stale_extra, "plAD");
  CHECK(stale_plad.has_value());

  const auto bytes =
      single_adjustment_layer_psd({{{'h', 'u', 'e', '2'}, test_hue2_payload(true, -157, 52, 0, 0, 0, 0)},
                                   {{'p', 'l', 'A', 'D'}, *stale_plad}});
  const auto document = patchy::psd::DocumentIo::read(bytes);
  const auto settings = patchy::adjustment_settings_from_layer(document.layers().front());
  CHECK(settings.has_value());
  CHECK(settings->kind == patchy::AdjustmentKind::HueSaturation);
  CHECK(settings->hue_saturation.colorize);
  CHECK(settings->hue_saturation.colorize_hue == 203);
  CHECK(settings->hue_saturation.hue_shift == 0);  // the stale plAD's 90 must lose
}

void psd_hue2_round_trip_patches_header_and_preserves_bands_and_tail() {
  // Non-default band settings prove the patch-in-place writer never regenerates them.
  const auto original = test_hue2_payload(true, -157, 52, 0, -180, -61, 0, true, 17);
  const auto bytes = single_adjustment_layer_psd({{{'h', 'u', 'e', '2'}, original}});
  auto document = patchy::psd::DocumentIo::read(bytes);

  // Pass 1: unchanged settings re-emit the payload byte-identically.
  const auto unchanged = psd_layer_block_payload(
      psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(document), 0), "hue2");
  CHECK(unchanged.has_value());
  CHECK(*unchanged == original);

  // Pass 2: editing the hue patches the header and keeps bands + trailer bytes.
  auto& layer = document.layers().front();
  auto settings = patchy::adjustment_settings_from_layer(layer);
  CHECK(settings.has_value());
  settings->hue_saturation.colorize_hue = 90;
  patchy::configure_adjustment_layer(layer, *settings);
  const auto patched = psd_layer_block_payload(
      psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(document), 0), "hue2");
  CHECK(patched.has_value());
  CHECK(patched->size() == original.size());
  const auto expected_header = test_hue2_payload(true, 90, 52, 0, -180, -61, 0);
  CHECK(std::equal(patched->begin(), patched->begin() + 16, expected_header.begin()));
  CHECK(std::equal(patched->begin() + 16, patched->end(), original.begin() + 16));
}

void psd_hue2_legacy_100_byte_payload_round_trips() {
  const auto original = test_hue2_payload(true, 30, 40, -10, 0, 0, 0, false);
  CHECK(original.size() == 100);
  const auto bytes = single_adjustment_layer_psd({{{'h', 'u', 'e', '2'}, original}});
  auto document = patchy::psd::DocumentIo::read(bytes);
  const auto settings = patchy::adjustment_settings_from_layer(document.layers().front());
  CHECK(settings.has_value());
  CHECK(settings->hue_saturation.colorize);
  CHECK(settings->hue_saturation.colorize_hue == 30);
  CHECK(settings->hue_saturation.colorize_saturation == 40);
  CHECK(settings->hue_saturation.colorize_lightness == -10);

  const auto rewritten = psd_layer_block_payload(
      psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(document), 0), "hue2");
  CHECK(rewritten.has_value());
  CHECK(*rewritten == original);  // patch-in-place keeps the tail-less length
}

void psd_patchy_adjustment_block_without_colorize_fields_still_parses() {
  // A pre-colorize plAD (version 4, exactly 30 i32s, 127 bytes) must load with
  // colorize defaults - the trailing extension is optional.
  patchy::psd::BigEndianWriter writer;
  writer.write_u8('P');
  writer.write_u8('L');
  writer.write_u8('A');
  writer.write_u8('D');
  writer.write_u16(4);
  writer.write_u8(2);  // AdjustmentKind::HueSaturation
  const auto write_i32 = [&writer](int value) {
    writer.write_u32(static_cast<std::uint32_t>(static_cast<std::int32_t>(value)));
  };
  for (int record = 0; record < 4; ++record) {
    write_i32(0);
    write_i32(255);
    write_i32(100);
    write_i32(0);
    write_i32(255);
  }
  write_i32(0);  // levels channel
  write_i32(0);
  write_i32(128);
  write_i32(255);
  write_i32(45);  // hue shift
  write_i32(10);
  write_i32(-5);
  write_i32(0);
  write_i32(0);
  write_i32(0);
  const auto payload = writer.bytes();
  CHECK(payload.size() == 127);

  const auto bytes = single_adjustment_layer_psd({{{'p', 'l', 'A', 'D'}, payload}});
  const auto document = patchy::psd::DocumentIo::read(bytes);
  const auto settings = patchy::adjustment_settings_from_layer(document.layers().front());
  CHECK(settings.has_value());
  CHECK(settings->kind == patchy::AdjustmentKind::HueSaturation);
  CHECK(settings->hue_saturation.hue_shift == 45);
  CHECK(settings->hue_saturation.saturation_delta == 10);
  CHECK(settings->hue_saturation.lightness_delta == -5);
  CHECK(!settings->hue_saturation.colorize);
  CHECK(settings->hue_saturation.colorize_saturation == 25);
}

void psd_photoshop_hue_saturation_colorize_fixture_matches_composite() {
  const auto path = patchy::test::committed_psd_fixture_path("photoshop-hue-saturation-colorize.psd");
  CHECK(std::filesystem::exists(path));

  const auto editable = patchy::psd::DocumentIo::read_file(path);
  const auto* adjustment = find_layer_named(editable.layers(), "Hue/Saturation 1");
  CHECK(adjustment != nullptr);
  CHECK(adjustment->kind() == patchy::LayerKind::Adjustment);
  CHECK(adjustment->mask().has_value());
  const auto settings = patchy::adjustment_settings_from_layer(*adjustment);
  CHECK(settings.has_value());
  CHECK(settings->hue_saturation.colorize);
  CHECK(settings->hue_saturation.colorize_hue == 203);
  CHECK(settings->hue_saturation.colorize_saturation == 52);

  patchy::psd::ReadOptions flat_options;
  flat_options.prefer_flat_composite = true;
  const auto photoshop_reference = patchy::psd::DocumentIo::read_file(path, flat_options);
  const auto reference_flat = patchy::Compositor{}.flatten_rgb8(photoshop_reference);
  const auto patchy_flat = patchy::Compositor{}.flatten_rgb8(editable);
  const auto metrics = rgb_diff_metrics(reference_flat, patchy_flat);
  CHECK(metrics.max_channel_delta <= 2);
  CHECK(metrics.mean_abs_channel_delta <= 0.1);
}

void psd_generic_bg_colorize_writes_comparison_artifacts_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("generic_bg.psd");
  if (!std::filesystem::exists(path)) {
    std::printf("[SKIP] psd_generic_bg_colorize_writes_comparison_artifacts_if_available (no local fixture)\n");
    return;
  }

  const auto editable = patchy::psd::DocumentIo::read_file(path);
  const auto* adjustment = find_layer_named(editable.layers(), "Hue/Saturation 1");
  CHECK(adjustment != nullptr);
  CHECK(adjustment->kind() == patchy::LayerKind::Adjustment);
  CHECK(adjustment->mask().has_value());
  const auto settings = patchy::adjustment_settings_from_layer(*adjustment);
  CHECK(settings.has_value());
  CHECK(settings->hue_saturation.colorize);
  CHECK(settings->hue_saturation.colorize_hue == 203);
  CHECK(settings->hue_saturation.colorize_saturation == 52);

  // generic_bg.psd carries no usable embedded composite (old save without a
  // real compatibility image), so pin PS-calibrated absolute colors instead:
  // panel bodies are gray 128 colorized with (203, 52, 0) -> (61, 140, 193)
  // per the docs/ps-compat.md calibration; the masked logo stays orange.
  const auto patchy_flat = patchy::Compositor{}.flatten_rgb8(editable);
  write_rgb8_bmp_artifact("psd_generic_bg_patchy_composite", patchy_flat);
  const auto pixel_close = [&](int x, int y, patchy::RgbColor expected, int tolerance) {
    const auto* actual = patchy_flat.pixel(x, y);
    return std::abs(int(expected.red) - int(actual[0])) <= tolerance &&
           std::abs(int(expected.green) - int(actual[1])) <= tolerance &&
           std::abs(int(expected.blue) - int(actual[2])) <= tolerance;
  };
  CHECK(pixel_close(150, 260, {61, 140, 193}, 2));  // Blip Tennis panel body (colorized blue)
  CHECK(pixel_close(420, 165, {61, 140, 193}, 2));  // Setup & Config panel body (colorized blue)
  CHECK(pixel_close(140, 60, {254, 154, 0}, 2));    // logo (masked out of the adjustment, stays orange)

  // Round trip: settings survive and the hue2 payload is byte-identical.
  const auto rewritten_bytes = patchy::psd::DocumentIo::write_layered_rgb8(editable);
  const auto round_tripped = patchy::psd::DocumentIo::read(rewritten_bytes);
  const auto* round_tripped_adjustment = find_layer_named(round_tripped.layers(), "Hue/Saturation 1");
  CHECK(round_tripped_adjustment != nullptr);
  const auto round_tripped_settings = patchy::adjustment_settings_from_layer(*round_tripped_adjustment);
  CHECK(round_tripped_settings.has_value());
  CHECK(round_tripped_settings->hue_saturation.colorize);
  CHECK(round_tripped_settings->hue_saturation.colorize_hue == 203);
  const auto original_hue2 = [&]() -> std::vector<std::uint8_t> {
    for (const auto& block : adjustment->unknown_psd_blocks()) {
      if (block.key == "hue2") {
        return block.payload;
      }
    }
    return {};
  }();
  const auto rewritten_hue2 = [&]() -> std::vector<std::uint8_t> {
    for (const auto& block : round_tripped_adjustment->unknown_psd_blocks()) {
      if (block.key == "hue2") {
        return block.payload;
      }
    }
    return {};
  }();
  CHECK(!original_hue2.empty());
  CHECK(original_hue2 == rewritten_hue2);
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

void adjustment_invert_math_lut_and_metadata_round_trip() {
  CHECK(patchy::adjustment_kind_key(patchy::AdjustmentKind::Invert) == "invert");
  const auto kind = patchy::adjustment_kind_from_key("invert");
  CHECK(kind.has_value());
  CHECK(*kind == patchy::AdjustmentKind::Invert);
  CHECK(patchy::adjustment_display_name(patchy::AdjustmentKind::Invert) == "Invert");

  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Invert;
  CHECK(patchy::adjustment_has_effect(settings));

  patchy::Layer layer(1, "Invert", patchy::LayerKind::Adjustment);
  patchy::configure_adjustment_layer(layer, settings);
  CHECK(patchy::layer_is_adjustment(layer));
  const auto restored = patchy::adjustment_settings_from_layer(layer);
  CHECK(restored.has_value());
  CHECK(restored->kind == patchy::AdjustmentKind::Invert);

  const auto inverted = patchy::apply_adjustment_to_color(patchy::RgbColor{1, 2, 3}, settings);
  CHECK(inverted.red == 254);
  CHECK(inverted.green == 253);
  CHECK(inverted.blue == 252);

  const auto lut = patchy::build_adjustment_lut(settings);
  CHECK(lut.has_value());
  for (int value = 0; value < 256; ++value) {
    const auto index = static_cast<std::size_t>(value);
    CHECK(lut->red[index] == 255 - value);
    CHECK(lut->green[index] == 255 - value);
    CHECK(lut->blue[index] == 255 - value);
  }

  // The destructive catalog filter shares the 255 - v formula; at its default
  // amount the two paths must stay byte-identical over a full-ramp buffer.
  auto adjusted = solid_rgb(16, 16, 0, 0, 0);
  auto filtered = solid_rgb(16, 16, 0, 0, 0);
  for (std::int32_t y = 0; y < 16; ++y) {
    for (std::int32_t x = 0; x < 16; ++x) {
      const auto value = static_cast<std::uint8_t>(y * 16 + x);
      auto* adjusted_px = adjusted.pixel(x, y);
      auto* filtered_px = filtered.pixel(x, y);
      adjusted_px[0] = filtered_px[0] = value;
      adjusted_px[1] = filtered_px[1] = static_cast<std::uint8_t>(255 - value);
      adjusted_px[2] = filtered_px[2] = static_cast<std::uint8_t>(value ^ 0x5A);
    }
  }
  patchy::apply_adjustment_to_pixels(adjusted, settings);
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  registry.apply(registry.default_invocation("patchy.filters.invert"), filtered);
  for (std::int32_t y = 0; y < 16; ++y) {
    for (std::int32_t x = 0; x < 16; ++x) {
      const auto* adjusted_px = adjusted.pixel(x, y);
      const auto* filtered_px = filtered.pixel(x, y);
      CHECK(adjusted_px[0] == filtered_px[0]);
      CHECK(adjusted_px[1] == filtered_px[1]);
      CHECK(adjusted_px[2] == filtered_px[2]);
    }
  }
}

void psd_invert_adjustment_writes_native_nvrt_only_and_round_trips() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(1, 1, 10, 200, 30));

  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Invert;
  patchy::Layer adjustment(document.allocate_layer_id(), "Invert", patchy::LayerKind::Adjustment);
  adjustment.set_bounds(patchy::Rect::from_size(document.width(), document.height()));
  patchy::configure_adjustment_layer(adjustment, settings);
  document.add_layer(std::move(adjustment));

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto extra_data = psd_layer_extra_data(bytes, 1);
  const auto nvrt = psd_layer_block_payload(extra_data, "nvrt");
  CHECK(nvrt.has_value());
  CHECK(nvrt->empty());
  // Kinds newer than plAD v4 are native-block only: an old build reading a
  // plAD with an unknown kind byte would misread the layer as Levels.
  CHECK(!psd_layer_block_payload(extra_data, "plAD").has_value());
  CHECK(!psd_layer_block_payload(extra_data, "levl").has_value());

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2);
  CHECK(read.layers()[1].kind() == patchy::LayerKind::Adjustment);
  const auto restored = patchy::adjustment_settings_from_layer(read.layers()[1]);
  CHECK(restored.has_value());
  CHECK(restored->kind == patchy::AdjustmentKind::Invert);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(read);
  CHECK(flattened.pixel(0, 0)[0] == 245);
  CHECK(flattened.pixel(0, 0)[1] == 55);
  CHECK(flattened.pixel(0, 0)[2] == 225);

  // A resave keeps the same native-only block shape.
  const auto resaved = patchy::psd::DocumentIo::write_layered_rgb8(read);
  const auto resaved_extra = psd_layer_extra_data(resaved, 1);
  CHECK(psd_layer_block_payload(resaved_extra, "nvrt").has_value());
  CHECK(!psd_layer_block_payload(resaved_extra, "plAD").has_value());
}

void psd_photoshop_invert_fixture_imports_renders_and_round_trips() {
  const auto path = patchy::test::committed_psd_fixture_path("photoshop-invert.psd");
  CHECK(std::filesystem::exists(path));

  const auto editable = patchy::psd::DocumentIo::read_file(path);
  const auto* adjustment = find_layer_named(editable.layers(), "Invert 1");
  CHECK(adjustment != nullptr);
  CHECK(adjustment->kind() == patchy::LayerKind::Adjustment);
  const auto settings = patchy::adjustment_settings_from_layer(*adjustment);
  CHECK(settings.has_value());
  CHECK(settings->kind == patchy::AdjustmentKind::Invert);

  // Invert is exactly 255 - v per channel in Photoshop too, so Patchy's
  // composite must match Photoshop's flattened result byte for byte.
  patchy::psd::ReadOptions flat_options;
  flat_options.prefer_flat_composite = true;
  const auto photoshop_reference = patchy::psd::DocumentIo::read_file(path, flat_options);
  const auto reference_flat = patchy::Compositor{}.flatten_rgb8(photoshop_reference);
  const auto patchy_flat = patchy::Compositor{}.flatten_rgb8(editable);
  const auto metrics = rgb_diff_metrics(reference_flat, patchy_flat);
  CHECK(metrics.max_channel_delta == 0);

  const auto resaved = patchy::psd::DocumentIo::write_layered_rgb8(editable);
  const auto extra = psd_layer_extra_data(resaved, 1);
  CHECK(psd_layer_block_payload(extra, "nvrt").has_value());
  CHECK(!psd_layer_block_payload(extra, "plAD").has_value());
  const auto reread = patchy::psd::DocumentIo::read(resaved);
  const auto* reread_adjustment = find_layer_named(reread.layers(), "Invert 1");
  CHECK(reread_adjustment != nullptr);
  const auto reread_settings = patchy::adjustment_settings_from_layer(*reread_adjustment);
  CHECK(reread_settings.has_value());
  CHECK(reread_settings->kind == patchy::AdjustmentKind::Invert);
}

void adjustment_posterize_threshold_math_lut_and_metadata_round_trip() {
  CHECK(patchy::adjustment_kind_key(patchy::AdjustmentKind::Posterize) == "posterize");
  CHECK(patchy::adjustment_kind_key(patchy::AdjustmentKind::Threshold) == "threshold");
  CHECK(patchy::adjustment_kind_from_key("posterize") == patchy::AdjustmentKind::Posterize);
  CHECK(patchy::adjustment_kind_from_key("threshold") == patchy::AdjustmentKind::Threshold);
  CHECK(patchy::adjustment_display_name(patchy::AdjustmentKind::Posterize) == "Posterize");
  CHECK(patchy::adjustment_display_name(patchy::AdjustmentKind::Threshold) == "Threshold");

  // Metadata round trip clamps to the model ranges.
  patchy::AdjustmentSettings posterize;
  posterize.kind = patchy::AdjustmentKind::Posterize;
  posterize.posterize.levels = 300;
  patchy::Layer posterize_layer(1, "Posterize", patchy::LayerKind::Adjustment);
  patchy::configure_adjustment_layer(posterize_layer, posterize);
  auto restored = patchy::adjustment_settings_from_layer(posterize_layer);
  CHECK(restored.has_value());
  CHECK(restored->kind == patchy::AdjustmentKind::Posterize);
  CHECK(restored->posterize.levels == 255);

  patchy::AdjustmentSettings threshold;
  threshold.kind = patchy::AdjustmentKind::Threshold;
  threshold.threshold.level = 0;
  patchy::Layer threshold_layer(2, "Threshold", patchy::LayerKind::Adjustment);
  patchy::configure_adjustment_layer(threshold_layer, threshold);
  restored = patchy::adjustment_settings_from_layer(threshold_layer);
  CHECK(restored.has_value());
  CHECK(restored->kind == patchy::AdjustmentKind::Threshold);
  CHECK(restored->threshold.level == 1);

  // Posterize bucket boundaries at levels 2 and 4.
  CHECK(patchy::posterize_channel_value(127, 2) == 0);
  CHECK(patchy::posterize_channel_value(128, 2) == 255);
  CHECK(patchy::posterize_channel_value(0, 4) == 0);
  CHECK(patchy::posterize_channel_value(60, 4) == 85);
  CHECK(patchy::posterize_channel_value(128, 4) == 170);
  CHECK(patchy::posterize_channel_value(255, 4) == 255);

  // Threshold decisions use the mixed luminance, pinned by a colored pixel
  // where a per-channel map would answer differently.
  threshold.threshold.level = 128;
  const auto red_result = patchy::apply_adjustment_to_color(patchy::RgbColor{255, 0, 0}, threshold);
  CHECK(red_result.red == 0);  // luminance 76 < 128 -> black despite red = 255
  const auto green_result = patchy::apply_adjustment_to_color(patchy::RgbColor{0, 255, 0}, threshold);
  CHECK(green_result.red == 255);  // luminance 150 >= 128 -> white
  CHECK(!patchy::build_adjustment_lut(threshold).has_value());
  CHECK(patchy::adjustment_has_effect(threshold));

  posterize.posterize.levels = 4;
  const auto posterize_lut = patchy::build_adjustment_lut(posterize);
  CHECK(posterize_lut.has_value());
  for (int value = 0; value < 256; ++value) {
    const auto expected = patchy::posterize_channel_value(static_cast<std::uint8_t>(value), 4);
    CHECK(posterize_lut->red[static_cast<std::size_t>(value)] == expected);
  }
  CHECK(patchy::adjustment_has_effect(posterize));

  // Destructive catalog parity over a deterministic ramp: the two paths share
  // one formula and must stay byte-identical (posterize at the destructive
  // range's edges, threshold at the shared default).
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  for (const int levels : {2, 4, 16}) {
    auto adjusted = solid_rgb(16, 16, 0, 0, 0);
    auto filtered = solid_rgb(16, 16, 0, 0, 0);
    for (std::int32_t y = 0; y < 16; ++y) {
      for (std::int32_t x = 0; x < 16; ++x) {
        const auto value = static_cast<std::uint8_t>(y * 16 + x);
        auto* adjusted_px = adjusted.pixel(x, y);
        auto* filtered_px = filtered.pixel(x, y);
        adjusted_px[0] = filtered_px[0] = value;
        adjusted_px[1] = filtered_px[1] = static_cast<std::uint8_t>(255 - value);
        adjusted_px[2] = filtered_px[2] = static_cast<std::uint8_t>(value ^ 0x5A);
      }
    }
    posterize.posterize.levels = levels;
    patchy::apply_adjustment_to_pixels(adjusted, posterize);
    auto invocation = registry.default_invocation("patchy.filters.posterize");
    invocation.parameters["levels"] = static_cast<std::int64_t>(levels);
    registry.apply(invocation, filtered);
    for (std::int32_t y = 0; y < 16; ++y) {
      for (std::int32_t x = 0; x < 16; ++x) {
        CHECK(std::memcmp(adjusted.pixel(x, y), filtered.pixel(x, y), 3) == 0);
      }
    }
  }
  {
    auto adjusted = solid_rgb(16, 16, 0, 0, 0);
    auto filtered = solid_rgb(16, 16, 0, 0, 0);
    for (std::int32_t y = 0; y < 16; ++y) {
      for (std::int32_t x = 0; x < 16; ++x) {
        const auto value = static_cast<std::uint8_t>(y * 16 + x);
        auto* adjusted_px = adjusted.pixel(x, y);
        auto* filtered_px = filtered.pixel(x, y);
        adjusted_px[0] = filtered_px[0] = value;
        adjusted_px[1] = filtered_px[1] = static_cast<std::uint8_t>(255 - value);
        adjusted_px[2] = filtered_px[2] = static_cast<std::uint8_t>(value ^ 0x5A);
      }
    }
    threshold.threshold.level = 128;
    patchy::apply_adjustment_to_pixels(adjusted, threshold);
    registry.apply(registry.default_invocation("patchy.filters.threshold"), filtered);
    for (std::int32_t y = 0; y < 16; ++y) {
      for (std::int32_t x = 0; x < 16; ++x) {
        CHECK(std::memcmp(adjusted.pixel(x, y), filtered.pixel(x, y), 3) == 0);
      }
    }
  }
}

void psd_posterize_threshold_write_native_blocks_and_round_trip() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(1, 1, 100, 100, 100));

  patchy::AdjustmentSettings posterize;
  posterize.kind = patchy::AdjustmentKind::Posterize;
  posterize.posterize.levels = 6;
  patchy::Layer posterize_layer(document.allocate_layer_id(), "Posterize", patchy::LayerKind::Adjustment);
  posterize_layer.set_bounds(patchy::Rect::from_size(document.width(), document.height()));
  patchy::configure_adjustment_layer(posterize_layer, posterize);
  document.add_layer(std::move(posterize_layer));

  patchy::AdjustmentSettings threshold;
  threshold.kind = patchy::AdjustmentKind::Threshold;
  threshold.threshold.level = 96;
  patchy::Layer threshold_layer(document.allocate_layer_id(), "Threshold", patchy::LayerKind::Adjustment);
  threshold_layer.set_bounds(patchy::Rect::from_size(document.width(), document.height()));
  patchy::configure_adjustment_layer(threshold_layer, threshold);
  document.add_layer(std::move(threshold_layer));

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto posterize_extra = psd_layer_extra_data(bytes, 1);
  const auto post = psd_layer_block_payload(posterize_extra, "post");
  CHECK(post.has_value());
  CHECK(post->size() == 4);
  CHECK((*post)[0] == 0 && (*post)[1] == 6 && (*post)[2] == 0 && (*post)[3] == 0);
  CHECK(!psd_layer_block_payload(posterize_extra, "plAD").has_value());
  const auto threshold_extra = psd_layer_extra_data(bytes, 2);
  const auto thrs = psd_layer_block_payload(threshold_extra, "thrs");
  CHECK(thrs.has_value());
  CHECK(thrs->size() == 4);
  CHECK((*thrs)[0] == 0 && (*thrs)[1] == 96 && (*thrs)[2] == 0 && (*thrs)[3] == 0);
  CHECK(!psd_layer_block_payload(threshold_extra, "plAD").has_value());

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 3);
  const auto read_posterize = patchy::adjustment_settings_from_layer(read.layers()[1]);
  CHECK(read_posterize.has_value());
  CHECK(read_posterize->kind == patchy::AdjustmentKind::Posterize);
  CHECK(read_posterize->posterize.levels == 6);
  const auto read_threshold = patchy::adjustment_settings_from_layer(read.layers()[2]);
  CHECK(read_threshold.has_value());
  CHECK(read_threshold->kind == patchy::AdjustmentKind::Threshold);
  CHECK(read_threshold->threshold.level == 96);
}

void psd_photoshop_posterize_threshold_fixtures_import_and_round_trip() {
  const auto posterize_path = patchy::test::committed_psd_fixture_path("photoshop-posterize.psd");
  CHECK(std::filesystem::exists(posterize_path));
  const auto posterize_doc = patchy::psd::DocumentIo::read_file(posterize_path);
  const auto* posterize_layer = find_layer_named(posterize_doc.layers(), "Posterize 1");
  CHECK(posterize_layer != nullptr);
  const auto posterize_settings = patchy::adjustment_settings_from_layer(*posterize_layer);
  CHECK(posterize_settings.has_value());
  CHECK(posterize_settings->kind == patchy::AdjustmentKind::Posterize);
  CHECK(posterize_settings->posterize.levels == 6);
  // An unedited resave re-emits the imported payload byte-for-byte.
  const auto posterize_resaved = patchy::psd::DocumentIo::write_layered_rgb8(posterize_doc);
  const auto resaved_post = psd_layer_block_payload(psd_layer_extra_data(posterize_resaved, 1), "post");
  CHECK(resaved_post.has_value());
  CHECK((*resaved_post)[1] == 6);

  const auto threshold_path = patchy::test::committed_psd_fixture_path("photoshop-threshold.psd");
  CHECK(std::filesystem::exists(threshold_path));
  const auto threshold_doc = patchy::psd::DocumentIo::read_file(threshold_path);
  const auto* threshold_layer = find_layer_named(threshold_doc.layers(), "Threshold 1");
  CHECK(threshold_layer != nullptr);
  const auto threshold_settings = patchy::adjustment_settings_from_layer(*threshold_layer);
  CHECK(threshold_settings.has_value());
  CHECK(threshold_settings->kind == patchy::AdjustmentKind::Threshold);
  CHECK(threshold_settings->threshold.level == 96);
  CHECK(threshold_layer->mask().has_value());

  // Every fixture color's luminance (117..148) sits far above level 96, so
  // Patchy's threshold decisions match Photoshop's on this file and the
  // composite comparison is exact.
  patchy::psd::ReadOptions flat_options;
  flat_options.prefer_flat_composite = true;
  const auto reference = patchy::psd::DocumentIo::read_file(threshold_path, flat_options);
  const auto reference_flat = patchy::Compositor{}.flatten_rgb8(reference);
  const auto patchy_flat = patchy::Compositor{}.flatten_rgb8(threshold_doc);
  const auto metrics = rgb_diff_metrics(reference_flat, patchy_flat);
  CHECK(metrics.max_channel_delta == 0);
}

void adjustment_brightness_contrast_math_lut_and_metadata_round_trip() {
  CHECK(patchy::adjustment_kind_key(patchy::AdjustmentKind::BrightnessContrast) == "brightness_contrast");
  CHECK(patchy::adjustment_kind_from_key("brightness_contrast") == patchy::AdjustmentKind::BrightnessContrast);
  CHECK(patchy::adjustment_display_name(patchy::AdjustmentKind::BrightnessContrast) == "Brightness/Contrast");

  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::BrightnessContrast;
  settings.brightness_contrast.brightness = 150;
  settings.brightness_contrast.contrast = -120;
  patchy::Layer layer(1, "Brightness/Contrast", patchy::LayerKind::Adjustment);
  patchy::configure_adjustment_layer(layer, settings);
  const auto restored = patchy::adjustment_settings_from_layer(layer);
  CHECK(restored.has_value());
  CHECK(restored->kind == patchy::AdjustmentKind::BrightnessContrast);
  CHECK(restored->brightness_contrast.brightness == 100);
  CHECK(restored->brightness_contrast.contrast == -100);

  // Points pinned exactly by the PS 2026 legacy-mode ramp captures.
  CHECK(patchy::brightness_contrast_channel_value(0, 30, 0) == 30);    // pure brightness is a plain add
  CHECK(patchy::brightness_contrast_channel_value(250, 30, 0) == 255);
  CHECK(patchy::brightness_contrast_channel_value(64, 0, 50) == 1);    // slope 2 around the 127.5 pivot
  CHECK(patchy::brightness_contrast_channel_value(65, 0, 50) == 3);
  CHECK(patchy::brightness_contrast_channel_value(69, 0, 50) == 11);
  CHECK(patchy::brightness_contrast_channel_value(126, 0, 100) == 0);  // c = 100 thresholds at v + b >= 127
  CHECK(patchy::brightness_contrast_channel_value(127, 0, 100) == 255);
  CHECK(patchy::brightness_contrast_channel_value(176, -50, 100) == 0);   // brightness folds into the input
  CHECK(patchy::brightness_contrast_channel_value(177, -50, 100) == 255);
  // Negative contrast adds brightness to the OUTPUT (model value; PS's own
  // LUT wobbles within +/-1 of this formula).
  CHECK(patchy::brightness_contrast_channel_value(1, -40, -40) == 12);

  // Deliberate divergence from the byte-pinned destructive filter: positive
  // contrast uses Photoshop's 100/(100-c) slope, not the catalog's linear
  // (100+c)/100. If this ever "unifies", pinned pixels change - keep both.
  CHECK(patchy::brightness_contrast_channel_value(64, 0, 50) !=
        static_cast<std::uint8_t>(std::clamp(std::lround((64.0 - 128.0) * 1.5 + 128.0), 0L, 255L)));

  settings.brightness_contrast.brightness = 20;
  settings.brightness_contrast.contrast = 35;
  const auto lut = patchy::build_adjustment_lut(settings);
  CHECK(lut.has_value());
  for (int value = 0; value < 256; ++value) {
    const auto expected = patchy::brightness_contrast_channel_value(static_cast<std::uint8_t>(value), 20, 35);
    CHECK(lut->red[static_cast<std::size_t>(value)] == expected);
    CHECK(lut->green[static_cast<std::size_t>(value)] == expected);
    CHECK(lut->blue[static_cast<std::size_t>(value)] == expected);
  }
  CHECK(patchy::adjustment_has_effect(settings));
  settings.brightness_contrast = patchy::BrightnessContrastAdjustment{};
  CHECK(!patchy::adjustment_has_effect(settings));
}

void psd_brightness_contrast_writes_native_brit_only_and_round_trips() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(1, 1, 100, 100, 100));

  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::BrightnessContrast;
  settings.brightness_contrast.brightness = 30;
  settings.brightness_contrast.contrast = -20;
  patchy::Layer adjustment(document.allocate_layer_id(), "Brightness/Contrast", patchy::LayerKind::Adjustment);
  adjustment.set_bounds(patchy::Rect::from_size(document.width(), document.height()));
  patchy::configure_adjustment_layer(adjustment, settings);
  document.add_layer(std::move(adjustment));

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto extra = psd_layer_extra_data(bytes, 1);
  const auto brit = psd_layer_block_payload(extra, "brit");
  CHECK(brit.has_value());
  CHECK(brit->size() == 8);
  // Photoshop 2026's legacy-mode shape: brightness i16, contrast i16,
  // mean 127, lab 0, pad 0.
  const std::array<std::uint8_t, 8> expected{0x00, 0x1E, 0xFF, 0xEC, 0x00, 0x7F, 0x00, 0x00};
  CHECK(std::equal(brit->begin(), brit->end(), expected.begin()));
  CHECK(!psd_layer_block_payload(extra, "CgEd").has_value());
  CHECK(!psd_layer_block_payload(extra, "plAD").has_value());

  const auto read = patchy::psd::DocumentIo::read(bytes);
  const auto restored = patchy::adjustment_settings_from_layer(read.layers()[1]);
  CHECK(restored.has_value());
  CHECK(restored->kind == patchy::AdjustmentKind::BrightnessContrast);
  CHECK(restored->brightness_contrast.brightness == 30);
  CHECK(restored->brightness_contrast.contrast == -20);
}

void psd_photoshop_brightness_contrast_fixtures_import_edit_and_preserve() {
  // Legacy fixture: 'brit' only, exact parameter recovery, and a composite
  // match within the calibrated +/-1 envelope.
  const auto legacy_path = patchy::test::committed_psd_fixture_path("photoshop-brightness-contrast-legacy.psd");
  CHECK(std::filesystem::exists(legacy_path));
  const auto legacy_doc = patchy::psd::DocumentIo::read_file(legacy_path);
  const auto* legacy_layer = find_layer_named(legacy_doc.layers(), "Brightness/Contrast 1");
  CHECK(legacy_layer != nullptr);
  const auto legacy_settings = patchy::adjustment_settings_from_layer(*legacy_layer);
  CHECK(legacy_settings.has_value());
  CHECK(legacy_settings->kind == patchy::AdjustmentKind::BrightnessContrast);
  CHECK(legacy_settings->brightness_contrast.brightness == 30);
  CHECK(legacy_settings->brightness_contrast.contrast == -20);

  patchy::psd::ReadOptions flat_options;
  flat_options.prefer_flat_composite = true;
  const auto legacy_reference = patchy::psd::DocumentIo::read_file(legacy_path, flat_options);
  const auto reference_flat = patchy::Compositor{}.flatten_rgb8(legacy_reference);
  const auto patchy_flat = patchy::Compositor{}.flatten_rgb8(legacy_doc);
  const auto metrics = rgb_diff_metrics(reference_flat, patchy_flat);
  CHECK(metrics.max_channel_delta <= 1);

  // Unedited resave re-emits the imported 'brit' byte-for-byte.
  const auto legacy_original_brit =
      psd_layer_block_payload(psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(legacy_doc), 1),
                              "brit");
  CHECK(legacy_original_brit.has_value());
  const std::array<std::uint8_t, 8> legacy_expected{0x00, 0x1E, 0xFF, 0xEC, 0x00, 0x7F, 0x00, 0x00};
  CHECK(legacy_original_brit->size() == 8);
  CHECK(std::equal(legacy_original_brit->begin(), legacy_original_brit->end(), legacy_expected.begin()));

  // Modern fixture: the CgEd descriptor is authoritative (the compatibility
  // 'brit' beside it is all zeros), values clamp into the legacy model, and
  // the read reports the algorithm approximation.
  const auto modern_path = patchy::test::committed_psd_fixture_path("photoshop-brightness-contrast-modern.psd");
  CHECK(std::filesystem::exists(modern_path));
  std::vector<std::string> notices;
  patchy::psd::ReadOptions notice_options;
  notice_options.notices = &notices;
  const auto modern_doc = patchy::psd::DocumentIo::read_file(modern_path, notice_options);
  const auto* modern_layer = find_layer_named(modern_doc.layers(), "Brightness/Contrast 1");
  CHECK(modern_layer != nullptr);
  const auto modern_settings = patchy::adjustment_settings_from_layer(*modern_layer);
  CHECK(modern_settings.has_value());
  CHECK(modern_settings->brightness_contrast.brightness == 40);
  CHECK(modern_settings->brightness_contrast.contrast == 25);
  CHECK(std::any_of(notices.begin(), notices.end(), [](const std::string& notice) {
    return notice.find("modern algorithm") != std::string::npos;
  }));

  // Unedited: both original blocks survive byte-for-byte.
  const auto modern_resaved = patchy::psd::DocumentIo::write_layered_rgb8(modern_doc);
  const auto modern_extra = psd_layer_extra_data(modern_resaved, 1);
  const auto modern_brit = psd_layer_block_payload(modern_extra, "brit");
  CHECK(modern_brit.has_value());
  CHECK(std::all_of(modern_brit->begin(), modern_brit->end(), [](std::uint8_t byte) { return byte == 0; }));
  CHECK(psd_layer_block_payload(modern_extra, "CgEd").has_value());

  // An edit regenerates legacy 'brit' and drops the stale CgEd descriptor
  // (Photoshop would otherwise keep reading the old modern values).
  auto edited_doc = patchy::psd::DocumentIo::read_file(modern_path);
  auto* edited_layer = edited_doc.find_layer(find_layer_named(edited_doc.layers(), "Brightness/Contrast 1")->id());
  CHECK(edited_layer != nullptr);
  auto edited_settings = *patchy::adjustment_settings_from_layer(*edited_layer);
  edited_settings.brightness_contrast.brightness = -15;
  edited_settings.brightness_contrast.contrast = 60;
  patchy::configure_adjustment_layer(*edited_layer, edited_settings);
  const auto edited_resaved = patchy::psd::DocumentIo::write_layered_rgb8(edited_doc);
  const auto edited_extra = psd_layer_extra_data(edited_resaved, 1);
  const auto edited_brit = psd_layer_block_payload(edited_extra, "brit");
  CHECK(edited_brit.has_value());
  CHECK(edited_brit->size() == 8);
  const std::array<std::uint8_t, 8> edited_expected{0xFF, 0xF1, 0x00, 0x3C, 0x00, 0x7F, 0x00, 0x00};
  CHECK(std::equal(edited_brit->begin(), edited_brit->end(), edited_expected.begin()));
  CHECK(!psd_layer_block_payload(edited_extra, "CgEd").has_value());
  const auto edited_read = patchy::psd::DocumentIo::read(edited_resaved);
  const auto edited_restored =
      patchy::adjustment_settings_from_layer(*find_layer_named(edited_read.layers(), "Brightness/Contrast 1"));
  CHECK(edited_restored.has_value());
  CHECK(edited_restored->brightness_contrast.brightness == -15);
  CHECK(edited_restored->brightness_contrast.contrast == 60);
}

void psd_color_balance_writes_native_blnc_only_and_round_trips() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(1, 1, 100, 100, 100));

  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::ColorBalance;
  settings.color_balance = patchy::ColorBalanceAdjustment{45, -25, 35};
  patchy::Layer adjustment(document.allocate_layer_id(), "Color Balance", patchy::LayerKind::Adjustment);
  adjustment.set_bounds(patchy::Rect::from_size(document.width(), document.height()));
  patchy::configure_adjustment_layer(adjustment, settings);
  document.add_layer(std::move(adjustment));

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto extra = psd_layer_extra_data(bytes, 1);
  const auto blnc = psd_layer_block_payload(extra, "blnc");
  CHECK(blnc.has_value());
  CHECK(blnc->size() == 20);
  // PS 2026's midtones-only shape: zero shadows, the model triple as
  // midtones, zero highlights, preserve luminosity off.
  const std::array<std::uint8_t, 20> expected{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D, 0xFF, 0xE7,
                                              0x00, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  CHECK(std::equal(blnc->begin(), blnc->end(), expected.begin()));
  // A plAD-only Color Balance opened in Photoshop as an opaque white NORMAL
  // raster layer; the private block is gone for good.
  CHECK(!psd_layer_block_payload(extra, "plAD").has_value());

  const auto read = patchy::psd::DocumentIo::read(bytes);
  const auto restored = patchy::adjustment_settings_from_layer(read.layers()[1]);
  CHECK(restored.has_value());
  CHECK(restored->kind == patchy::AdjustmentKind::ColorBalance);
  CHECK(restored->color_balance.cyan_red == 45);
  CHECK(restored->color_balance.magenta_green == -25);
  CHECK(restored->color_balance.yellow_blue == 35);
}

void psd_legacy_plad_color_balance_migrates_to_native_blnc() {
  patchy::psd::BigEndianWriter writer;
  writer.write_bytes(std::array<std::uint8_t, 4>{'P', 'L', 'A', 'D'});
  writer.write_u16(4);
  writer.write_u8(3);  // Color Balance.
  const auto write_i32 = [&writer](int value) { writer.write_u32(static_cast<std::uint32_t>(value)); };
  for (int channel = 0; channel < 4; ++channel) {
    write_i32(0);
    write_i32(255);
    write_i32(100);
    write_i32(0);
    write_i32(255);
  }
  write_i32(0);    // Levels channel.
  write_i32(0);    // Curve shadow output.
  write_i32(128);  // Curve midtone output.
  write_i32(255);  // Curve highlight output.
  write_i32(0);    // Hue shift.
  write_i32(0);    // Saturation delta.
  write_i32(0);    // Lightness delta.
  write_i32(50);   // Cyan/red.
  write_i32(-10);  // Magenta/green.
  write_i32(20);   // Yellow/blue.
  write_i32(0);    // Colorize disabled.
  write_i32(0);
  write_i32(25);
  write_i32(0);

  const auto bytes = single_adjustment_layer_psd({{{'p', 'l', 'A', 'D'}, writer.bytes()}});
  const auto document = patchy::psd::DocumentIo::read(bytes);
  CHECK(document.layers().size() == 1);
  const auto settings = patchy::adjustment_settings_from_layer(document.layers().front());
  CHECK(settings.has_value());
  CHECK(settings->kind == patchy::AdjustmentKind::ColorBalance);
  CHECK(settings->color_balance.cyan_red == 50);

  const auto migrated = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto migrated_extra = psd_layer_extra_data(migrated, 0);
  CHECK(!psd_layer_block_payload(migrated_extra, "plAD").has_value());
  const auto blnc = psd_layer_block_payload(migrated_extra, "blnc");
  CHECK(blnc.has_value());
  const auto migrated_settings =
      patchy::adjustment_settings_from_layer(patchy::psd::DocumentIo::read(migrated).layers().front());
  CHECK(migrated_settings.has_value());
  CHECK(migrated_settings->kind == patchy::AdjustmentKind::ColorBalance);
  CHECK(migrated_settings->color_balance.cyan_red == 50);
  CHECK(migrated_settings->color_balance.magenta_green == -10);
  CHECK(migrated_settings->color_balance.yellow_blue == 20);
}

void psd_photoshop_color_balance_fixtures_import_patch_and_preserve() {
  const auto path = patchy::test::committed_psd_fixture_path("photoshop-color-balance.psd");
  CHECK(std::filesystem::exists(path));
  std::vector<std::string> notices;
  patchy::psd::ReadOptions options;
  options.notices = &notices;
  const auto document = patchy::psd::DocumentIo::read_file(path, options);
  const auto* layer = find_layer_named(document.layers(), "Color Balance 1");
  CHECK(layer != nullptr);
  const auto settings = patchy::adjustment_settings_from_layer(*layer);
  CHECK(settings.has_value());
  CHECK(settings->kind == patchy::AdjustmentKind::ColorBalance);
  CHECK(settings->color_balance.cyan_red == 45);
  CHECK(settings->color_balance.magenta_green == -25);
  CHECK(settings->color_balance.yellow_blue == 35);
  // Midtones-only with preserve luminosity off: nothing unrendered.
  CHECK(std::none_of(notices.begin(), notices.end(), [](const std::string& notice) {
    return notice.find("does not render") != std::string::npos;
  }));
  // Unedited resave is byte-identical (patching identical midtones is a no-op).
  const auto resaved = psd_layer_block_payload(
      psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(document), 1), "blnc");
  CHECK(resaved.has_value());
  const std::array<std::uint8_t, 20> expected{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D, 0xFF, 0xE7,
                                              0x00, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  CHECK(resaved->size() == 20);
  CHECK(std::equal(resaved->begin(), resaved->end(), expected.begin()));

  // The full fixture carries shadows/highlights and preserve luminosity:
  // modeled midtones import, the rest is preserved and reported.
  const auto full_path = patchy::test::committed_psd_fixture_path("photoshop-color-balance-full.psd");
  CHECK(std::filesystem::exists(full_path));
  std::vector<std::string> full_notices;
  patchy::psd::ReadOptions full_options;
  full_options.notices = &full_notices;
  auto full_document = patchy::psd::DocumentIo::read_file(full_path, full_options);
  const auto* full_layer = find_layer_named(full_document.layers(), "Color Balance 1");
  CHECK(full_layer != nullptr);
  const auto full_settings = patchy::adjustment_settings_from_layer(*full_layer);
  CHECK(full_settings.has_value());
  CHECK(full_settings->color_balance.cyan_red == 45);
  CHECK(std::any_of(full_notices.begin(), full_notices.end(), [](const std::string& notice) {
    return notice.find("does not render") != std::string::npos;
  }));
  const auto full_original{psd_layer_block_payload(
      psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(full_document), 1), "blnc")};
  CHECK(full_original.has_value());
  const std::array<std::uint8_t, 20> full_expected{0xFF, 0xF6, 0x00, 0x05, 0x00, 0x0F, 0x00, 0x2D, 0xFF, 0xE7,
                                                   0x00, 0x23, 0xFF, 0xE2, 0x00, 0x14, 0xFF, 0xFB, 0x01, 0x00};
  CHECK(full_original->size() == 20);
  CHECK(std::equal(full_original->begin(), full_original->end(), full_expected.begin()));

  // A midtones edit patches in place: shadows, highlights, and the preserve
  // luminosity byte keep their imported values.
  auto* editable = full_document.find_layer(full_layer->id());
  CHECK(editable != nullptr);
  auto edited_settings = *full_settings;
  edited_settings.color_balance = patchy::ColorBalanceAdjustment{-60, 15, 0};
  patchy::configure_adjustment_layer(*editable, edited_settings);
  const auto edited = psd_layer_block_payload(
      psd_layer_extra_data(patchy::psd::DocumentIo::write_layered_rgb8(full_document), 1), "blnc");
  CHECK(edited.has_value());
  const std::array<std::uint8_t, 20> edited_expected{0xFF, 0xF6, 0x00, 0x05, 0x00, 0x0F, 0xFF, 0xC4, 0x00, 0x0F,
                                                     0x00, 0x00, 0xFF, 0xE2, 0x00, 0x14, 0xFF, 0xFB, 0x01, 0x00};
  CHECK(edited->size() == 20);
  CHECK(std::equal(edited->begin(), edited->end(), edited_expected.begin()));
}

void psd_native_nvrt_adjustment_imports_and_renders() {
  const auto bytes = single_adjustment_layer_psd({{{'n', 'v', 'r', 't'}, {}}});
  const auto document = patchy::psd::DocumentIo::read(bytes);
  CHECK(document.layers().size() == 1);
  CHECK(document.layers().front().kind() == patchy::LayerKind::Adjustment);
  const auto settings = patchy::adjustment_settings_from_layer(document.layers().front());
  CHECK(settings.has_value());
  CHECK(settings->kind == patchy::AdjustmentKind::Invert);

  patchy::Document composited(1, 1, patchy::PixelFormat::rgb8());
  composited.add_pixel_layer("Base", solid_rgb(1, 1, 0, 200, 64));
  patchy::Layer adjustment(composited.allocate_layer_id(), "Invert", patchy::LayerKind::Adjustment);
  patchy::configure_adjustment_layer(adjustment, *settings);
  composited.add_layer(std::move(adjustment));
  const auto flattened = patchy::Compositor{}.flatten_rgb8(composited);
  CHECK(flattened.pixel(0, 0)[0] == 255);
  CHECK(flattened.pixel(0, 0)[1] == 55);
  CHECK(flattened.pixel(0, 0)[2] == 191);
}

}  // namespace

std::vector<patchy::test::TestCase> adjustments_curves_tests() {
  return {
      {"curves_control_points_normalize_and_build_composed_luts",
       curves_control_points_normalize_and_build_composed_luts},
      {"curves_lut_matches_photoshop_2026_calibration", curves_lut_matches_photoshop_2026_calibration},
      {"curves_eyedroppers_rebuild_neutral_component_curves",
       curves_eyedroppers_rebuild_neutral_component_curves},
      {"acv_v4_reads_photoshop_rgb_order_and_writes_canonical_bytes",
       acv_v4_reads_photoshop_rgb_order_and_writes_canonical_bytes},
      {"acv_v1_reads_photoshop_bitmap_and_indexed_extension",
       acv_v1_reads_photoshop_bitmap_and_indexed_extension},
      {"acv_rejects_malformed_data_and_invalid_models", acv_rejects_malformed_data_and_invalid_models},
      {"curves_lut_export_targets_match_reference_and_preserve_alpha",
       curves_lut_export_targets_match_reference_and_preserve_alpha},
      {"curves_metadata_prefers_rich_points_and_keeps_legacy_fallback",
       curves_metadata_prefers_rich_points_and_keeps_legacy_fallback},
      {"psd_curves_native_output_omits_private_plad_and_migrates_legacy",
       psd_curves_native_output_omits_private_plad_and_migrates_legacy},
      {"psd_curves_private_plad_v4_imports_and_malformed_tail_migrates_fallback",
       psd_curves_private_plad_v4_imports_and_malformed_tail_migrates_fallback},
      {"psd_native_curves_suppresses_private_fallback_and_preserves_both_blocks",
       psd_native_curves_suppresses_private_fallback_and_preserves_both_blocks},
      {"psd_photoshop_curves_fixtures_import_preserve_regenerate_and_round_trip",
       psd_photoshop_curves_fixtures_import_preserve_regenerate_and_round_trip},
      {"psd_levels_adjustment_channel_round_trips", psd_levels_adjustment_channel_round_trips},
      {"psd_levels_adjustment_writes_native_levl_and_patchy_fallback",
       psd_levels_adjustment_writes_native_levl_and_patchy_fallback},
      {"psd_native_levels_adjustment_imports_without_patchy_block",
       psd_native_levels_adjustment_imports_without_patchy_block},
      {"psd_native_levels_overrides_stale_patchy_fallback",
       psd_native_levels_overrides_stale_patchy_fallback},
      {"adjustment_hue_saturation_colorize_matches_photoshop_reference",
       adjustment_hue_saturation_colorize_matches_photoshop_reference},
      {"psd_native_hue2_colorize_adjustment_imports_and_renders",
       psd_native_hue2_colorize_adjustment_imports_and_renders},
      {"psd_hue_saturation_adjustment_writes_native_hue2_and_patchy_fallback",
       psd_hue_saturation_adjustment_writes_native_hue2_and_patchy_fallback},
      {"psd_native_hue2_overrides_stale_patchy_fallback", psd_native_hue2_overrides_stale_patchy_fallback},
      {"psd_hue2_round_trip_patches_header_and_preserves_bands_and_tail",
       psd_hue2_round_trip_patches_header_and_preserves_bands_and_tail},
      {"psd_hue2_legacy_100_byte_payload_round_trips", psd_hue2_legacy_100_byte_payload_round_trips},
      {"psd_patchy_adjustment_block_without_colorize_fields_still_parses",
       psd_patchy_adjustment_block_without_colorize_fields_still_parses},
      {"psd_photoshop_hue_saturation_colorize_fixture_matches_composite",
       psd_photoshop_hue_saturation_colorize_fixture_matches_composite},
      {"psd_generic_bg_colorize_writes_comparison_artifacts_if_available",
       psd_generic_bg_colorize_writes_comparison_artifacts_if_available},
      {"psd_writer_uses_photoshop_bottom_to_top_layer_record_order",
       psd_writer_uses_photoshop_bottom_to_top_layer_record_order},
      {"adjustment_invert_math_lut_and_metadata_round_trip",
       adjustment_invert_math_lut_and_metadata_round_trip},
      {"psd_invert_adjustment_writes_native_nvrt_only_and_round_trips",
       psd_invert_adjustment_writes_native_nvrt_only_and_round_trips},
      {"psd_native_nvrt_adjustment_imports_and_renders", psd_native_nvrt_adjustment_imports_and_renders},
      {"psd_photoshop_invert_fixture_imports_renders_and_round_trips",
       psd_photoshop_invert_fixture_imports_renders_and_round_trips},
      {"adjustment_posterize_threshold_math_lut_and_metadata_round_trip",
       adjustment_posterize_threshold_math_lut_and_metadata_round_trip},
      {"psd_posterize_threshold_write_native_blocks_and_round_trip",
       psd_posterize_threshold_write_native_blocks_and_round_trip},
      {"psd_photoshop_posterize_threshold_fixtures_import_and_round_trip",
       psd_photoshop_posterize_threshold_fixtures_import_and_round_trip},
      {"adjustment_brightness_contrast_math_lut_and_metadata_round_trip",
       adjustment_brightness_contrast_math_lut_and_metadata_round_trip},
      {"psd_brightness_contrast_writes_native_brit_only_and_round_trips",
       psd_brightness_contrast_writes_native_brit_only_and_round_trips},
      {"psd_photoshop_brightness_contrast_fixtures_import_edit_and_preserve",
       psd_photoshop_brightness_contrast_fixtures_import_edit_and_preserve},
      {"psd_color_balance_writes_native_blnc_only_and_round_trips",
       psd_color_balance_writes_native_blnc_only_and_round_trips},
      {"psd_legacy_plad_color_balance_migrates_to_native_blnc",
       psd_legacy_plad_color_balance_migrates_to_native_blnc},
      {"psd_photoshop_color_balance_fixtures_import_patch_and_preserve",
       psd_photoshop_color_balance_fixtures_import_patch_and_preserve},
  };
}
