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

using patchy::test::fnv1a_hash_bytes;
using patchy::test::read_binary_file;
using patchy::test::write_ascii4;

struct TestPatPlane {
  std::uint32_t slot{0};
  std::int32_t top{0};
  std::int32_t left{0};
  std::int32_t bottom{0};
  std::int32_t right{0};
  std::uint16_t depth{8};
  std::uint8_t compression{0};
  std::vector<std::uint8_t> data;
};

std::vector<std::uint8_t> test_color_pat_bytes(
    std::uint32_t mode, std::uint16_t width, std::uint16_t height,
    std::string_view name, const std::string& id, std::span<const TestPatPlane> planes) {
  patchy::psd::BigEndianWriter writer;
  write_ascii4(writer, "8BPT");
  writer.write_u16(1);  // Standalone PAT file version.
  writer.write_u32(1);  // Pattern count.

  writer.write_u32(1);  // Pattern record version.
  writer.write_u32(mode);
  writer.write_u16(height);
  writer.write_u16(width);
  patchy::psd::write_descriptor_unicode_string(writer, name);
  CHECK(id.size() <= 255U);
  writer.write_u8(static_cast<std::uint8_t>(id.size()));
  writer.write_bytes(std::span(reinterpret_cast<const std::uint8_t*>(id.data()), id.size()));

  constexpr std::uint32_t kDeclaredChannels = 24;
  constexpr std::size_t kSlotCount = kDeclaredChannels + 2U;
  std::size_t vma_length = 16U + 4U;
  for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
    const auto plane = std::find_if(planes.begin(), planes.end(), [slot](const TestPatPlane& value) {
      return value.slot == slot;
    });
    vma_length += plane == planes.end() ? 4U : 8U + 23U + plane->data.size();
  }
  writer.write_u32(3);  // Virtual Memory Array version.
  writer.write_u32(static_cast<std::uint32_t>(vma_length));
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(height);
  writer.write_u32(width);
  writer.write_u32(kDeclaredChannels);

  for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
    const auto plane = std::find_if(planes.begin(), planes.end(), [slot](const TestPatPlane& value) {
      return value.slot == slot;
    });
    if (plane == planes.end()) {
      writer.write_u32(0);  // Unwritten user-mask/transparency or spare slot.
      continue;
    }
    writer.write_u32(1);  // Written.
    writer.write_u32(static_cast<std::uint32_t>(23U + plane->data.size()));
    writer.write_u32(plane->depth);
    writer.write_u32(static_cast<std::uint32_t>(plane->top));
    writer.write_u32(static_cast<std::uint32_t>(plane->left));
    writer.write_u32(static_cast<std::uint32_t>(plane->bottom));
    writer.write_u32(static_cast<std::uint32_t>(plane->right));
    writer.write_u16(plane->depth);
    writer.write_u8(plane->compression);
    writer.write_bytes(plane->data);
  }
  return writer.bytes();
}

std::vector<std::uint8_t> test_raw_rgb_pat_bytes(
    std::string id = "11111111-2222-3333-4444-555555555555") {
  std::vector<TestPatPlane> planes{
      {0, 0, 0, 2, 2, 8, 0, {10, 20, 30, 40}},
      {1, 0, 0, 2, 2, 8, 0, {50, 60, 70, 80}},
      {2, 0, 0, 2, 2, 8, 0, {90, 100, 110, 120}},
  };
  return test_color_pat_bytes(3, 2, 2, "Raw RGB", id, planes);
}

void write_test_u32_at(std::vector<std::uint8_t>& bytes, std::size_t offset,
                       std::uint32_t value) {
  CHECK(offset <= bytes.size() && bytes.size() - offset >= 4U);
  bytes[offset] = static_cast<std::uint8_t>(value >> 24U);
  bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 16U);
  bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 8U);
  bytes[offset + 3U] = static_cast<std::uint8_t>(value);
}

void append_pat_record(std::vector<std::uint8_t>& destination,
                       std::span<const std::uint8_t> source_pat) {
  CHECK(source_pat.size() >= 10U);
  destination.insert(destination.end(), source_pat.begin() + 10, source_pat.end());
}

std::vector<std::uint8_t> pat_record_as_patterns_block(
    std::span<const std::uint8_t> pat) {
  CHECK(pat.size() >= 10U);
  patchy::psd::BigEndianWriter writer;
  const auto record = pat.subspan(10U);
  writer.write_u32(static_cast<std::uint32_t>(record.size()));
  writer.write_bytes(record);
  while (writer.bytes().size() % 4U != 0U) {
    writer.write_u8(0U);
  }
  return writer.bytes();
}

std::size_t test_pat_vma_length_offset(std::span<const std::uint8_t> pat) {
  patchy::psd::BigEndianReader reader(pat);
  reader.skip(10U);  // file envelope
  reader.skip(12U);  // record version, mode, and dimensions
  (void)patchy::psd::read_descriptor_unicode_string(reader);
  reader.skip(reader.read_u8());
  reader.skip(4U);  // VMA version
  return reader.position();
}

std::vector<std::uint8_t> test_indexed_pat_bytes(
    std::int32_t top = 0, std::int32_t left = 0, std::int32_t bottom = 2,
    std::int32_t right = 2) {
  patchy::psd::BigEndianWriter writer;
  write_ascii4(writer, "8BPT");
  writer.write_u16(1);
  writer.write_u32(1);
  writer.write_u32(1);
  writer.write_u32(2);  // Indexed.
  writer.write_u16(2);
  writer.write_u16(2);
  patchy::psd::write_descriptor_unicode_string(writer, "Indexed Alpha");
  constexpr std::string_view id = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
  writer.write_u8(static_cast<std::uint8_t>(id.size()));
  writer.write_bytes(std::span(reinterpret_cast<const std::uint8_t*>(id.data()), id.size()));

  std::array<std::uint8_t, 256U * 3U> table{};
  table[3] = 10;
  table[4] = 20;
  table[5] = 30;
  table[6] = 200;
  table[7] = 150;
  table[8] = 100;
  writer.write_bytes(table);
  writer.write_u16(3);  // Colors used.
  writer.write_u16(2);  // Palette index 2 is transparent.

  constexpr std::uint32_t kDeclaredChannels = 24;
  constexpr std::size_t kPixelCount = 4;
  constexpr std::size_t kChannelPayloadBytes = 23U + kPixelCount;
  constexpr std::size_t kSlotCount = kDeclaredChannels + 2U;
  constexpr std::size_t kVmaLength =
      16U + 4U + 8U + kChannelPayloadBytes + (kSlotCount - 1U) * 4U;
  writer.write_u32(3);
  writer.write_u32(static_cast<std::uint32_t>(kVmaLength));
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(2);
  writer.write_u32(2);
  writer.write_u32(kDeclaredChannels);
  writer.write_u32(1);
  writer.write_u32(static_cast<std::uint32_t>(kChannelPayloadBytes));
  writer.write_u32(8);
  writer.write_u32(static_cast<std::uint32_t>(top));
  writer.write_u32(static_cast<std::uint32_t>(left));
  writer.write_u32(static_cast<std::uint32_t>(bottom));
  writer.write_u32(static_cast<std::uint32_t>(right));
  writer.write_u16(8);
  writer.write_u8(0);
  constexpr std::array<std::uint8_t, 4> indices{1, 2, 2, 1};
  writer.write_bytes(indices);
  for (std::size_t slot = 1; slot < kSlotCount; ++slot) {
    writer.write_u32(0);
  }
  return writer.bytes();
}

void pat_hue_fixture_decodes_packbits_and_alpha() {
  const auto bytes =
      read_binary_file(patchy::test::committed_format_fixture_path("pat", "hue.pat"));
  CHECK(bytes.size() == 18367U);
  CHECK(fnv1a_hash_bytes(bytes) == 0x831FD2A322BE6A5EULL);
  std::string error;
  const auto result = patchy::psd::read_pat(bytes, error);
  CHECK(result.has_value());
  CHECK(error.empty());
  CHECK(result->warnings.empty());
  CHECK(result->patterns.size() == 1U);

  const auto& pattern = result->patterns.front();
  CHECK(pattern.name == "hue");
  CHECK(pattern.id == "e7f7ad0e-6537-0b48-a350-1a98c4160671");
  CHECK(pattern.provenance == patchy::PatternProvenance::Authored);
  CHECK(pattern.tile.width() == 100);
  CHECK(pattern.tile.height() == 100);
  CHECK(pattern.tile.format() == patchy::PixelFormat::rgba8());

  const auto check_pixel = [&pattern](int x, int y, std::array<std::uint8_t, 4> expected) {
    const auto* pixel = pattern.tile.pixel(x, y);
    CHECK(std::equal(expected.begin(), expected.end(), pixel));
  };
  check_pixel(0, 0, {255, 167, 0, 255});
  check_pixel(50, 50, {255, 77, 0, 255});
  check_pixel(25, 75, {255, 0, 0, 18});
  check_pixel(99, 99, {0, 17, 149, 255});
}

void pat_raw_rgb_record_imports_without_psd_length_wrapper() {
  const auto bytes = test_raw_rgb_pat_bytes();
  std::string error;
  const auto result = patchy::psd::read_pat(bytes, error);
  CHECK(result.has_value());
  CHECK(error.empty());
  CHECK(result->warnings.empty());
  CHECK(result->patterns.size() == 1U);

  const auto& pattern = result->patterns.front();
  CHECK(pattern.name == "Raw RGB");
  CHECK(pattern.id == "11111111-2222-3333-4444-555555555555");
  CHECK(pattern.tile.width() == 2);
  CHECK(pattern.tile.height() == 2);
  const auto* first = pattern.tile.pixel(0, 0);
  CHECK(first[0] == 10 && first[1] == 50 && first[2] == 90 && first[3] == 255);
  const auto* last = pattern.tile.pixel(1, 1);
  CHECK(last[0] == 40 && last[1] == 80 && last[2] == 120 && last[3] == 255);
}

void pat_indexed_footer_applies_transparent_palette_index() {
  const auto bytes = test_indexed_pat_bytes();
  std::string error;
  const auto result = patchy::psd::read_pat(bytes, error);
  CHECK(result.has_value());
  CHECK(error.empty());
  CHECK(result->warnings.empty());
  CHECK(result->patterns.size() == 1U);
  const auto& tile = result->patterns.front().tile;
  const auto* opaque = tile.pixel(0, 0);
  CHECK(opaque[0] == 10 && opaque[1] == 20 && opaque[2] == 30 && opaque[3] == 255);
  const auto* transparent = tile.pixel(1, 0);
  CHECK(transparent[0] == 200 && transparent[1] == 150 && transparent[2] == 100 &&
        transparent[3] == 0);
}

void pat_rejects_wrong_magic_version_truncation_and_empty_files() {
  const auto expect_rejected = [](std::span<const std::uint8_t> bytes) {
    std::string error;
    const auto result = patchy::psd::read_pat(bytes, error);
    CHECK(!result.has_value());
    CHECK(!error.empty());
  };

  expect_rejected({});

  auto wrong_magic = test_raw_rgb_pat_bytes();
  wrong_magic[0] = 'X';
  expect_rejected(wrong_magic);

  auto wrong_version = test_raw_rgb_pat_bytes();
  wrong_version[4] = 0;
  wrong_version[5] = 2;
  expect_rejected(wrong_version);

  auto truncated = test_raw_rgb_pat_bytes();
  truncated.pop_back();
  expect_rejected(truncated);

  const std::array<std::uint8_t, 10> no_patterns{
      '8', 'B', 'P', 'T', 0, 1, 0, 0, 0, 0,
  };
  expect_rejected(no_patterns);
}

void pat_gray16_and_cmyk_records_decode() {
  {
    const std::vector<TestPatPlane> planes{
        {0, 0, 0, 1, 3, 16, 0, {0x00, 0x00, 0x40, 0x00, 0x80, 0x00}},
    };
    const auto bytes = test_color_pat_bytes(
        1, 3, 1, "Gray 16", "22222222-3333-4444-5555-666666666666", planes);
    std::string error;
    const auto result = patchy::psd::read_pat(bytes, error);
    CHECK(result.has_value());
    CHECK(error.empty());
    CHECK(result->warnings.empty());
    CHECK(result->patterns.size() == 1U);
    const auto& tile = result->patterns.front().tile;
    CHECK(tile.pixel(0, 0)[0] == 0);
    CHECK(tile.pixel(1, 0)[0] == 128);
    CHECK(tile.pixel(2, 0)[0] == 255);
    for (int x = 0; x < 3; ++x) {
      const auto* pixel = tile.pixel(x, 0);
      CHECK(pixel[0] == pixel[1] && pixel[1] == pixel[2] && pixel[3] == 255);
    }
  }

  {
    const std::vector<TestPatPlane> planes{
        {0, 0, 0, 1, 2, 8, 0, {255, 255}},  // inverted cyan
        {1, 0, 0, 1, 2, 8, 0, {0, 255}},    // inverted magenta
        {2, 0, 0, 1, 2, 8, 0, {0, 255}},    // inverted yellow
        {3, 0, 0, 1, 2, 8, 0, {255, 128}},  // inverted black
    };
    const auto bytes = test_color_pat_bytes(
        4, 2, 1, "CMYK", "33333333-4444-5555-6666-777777777777", planes);
    std::string error;
    const auto result = patchy::psd::read_pat(bytes, error);
    CHECK(result.has_value());
    CHECK(error.empty());
    CHECK(result->warnings.empty());
    CHECK(result->patterns.size() == 1U);
    const auto& tile = result->patterns.front().tile;
    const auto* red = tile.pixel(0, 0);
    CHECK(red[0] == 255 && red[1] == 0 && red[2] == 0 && red[3] == 255);
    const auto* gray = tile.pixel(1, 0);
    CHECK(gray[0] == 128 && gray[1] == 128 && gray[2] == 128 && gray[3] == 255);
  }
}

void pat_invalid_utf8_id_is_replaced() {
  std::string invalid_id = "44444444-5555-6666-7777-888888888888";
  invalid_id[0] = static_cast<char>(0xFF);
  const auto bytes = test_raw_rgb_pat_bytes(invalid_id);
  std::string error;
  const auto result = patchy::psd::read_pat(bytes, error);
  CHECK(result.has_value());
  CHECK(error.empty());
  CHECK(result->patterns.size() == 1U);
  CHECK(result->warnings.size() == 1U);
  CHECK(result->warnings.front().find("invalid UTF-8 pattern id") != std::string::npos);
  const auto& replacement = result->patterns.front().id;
  CHECK(replacement != invalid_id);
  CHECK(replacement.size() == 36U);
  CHECK(replacement[8] == '-' && replacement[13] == '-' && replacement[18] == '-' &&
        replacement[23] == '-');
}

void pat_trailing_hierarchy_is_ignored() {
  auto bytes = test_raw_rgb_pat_bytes();
  constexpr std::array<std::uint8_t, 16> hierarchy{
      '8', 'B', 'I', 'M', 'p', 'h', 'r', 'y', 0, 0, 0, 4, 0, 0, 0, 0,
  };
  bytes.insert(bytes.end(), hierarchy.begin(), hierarchy.end());
  std::string error;
  const auto result = patchy::psd::read_pat(bytes, error);
  CHECK(result.has_value());
  CHECK(error.empty());
  CHECK(result->warnings.empty());
  CHECK(result->patterns.size() == 1U);
}

void pat_bounds_compressed_expansion_and_extreme_rectangles() {
  auto bytes = test_raw_rgb_pat_bytes();
  write_test_u32_at(bytes, 6U, 3U);

  // A tiny PackBits slot advertising just over eight million expanded pixels
  // must be rejected before the shared decoder can allocate its output plane.
  const std::vector<TestPatPlane> expansion_planes{
      {0, 0, 0, 1025, 8192, 8, 1, {}},
  };
  const auto expansion = test_color_pat_bytes(
      3, 1, 1, "Expansion", "55555555-6666-7777-8888-999999999999",
      expansion_planes);
  append_pat_record(bytes, expansion);

  // The mathematical width exceeds int32; subtraction must be widened before
  // applying the dimension guard rather than overflowing in signed arithmetic.
  const std::vector<TestPatPlane> extreme_planes{
      {0, 0, std::numeric_limits<std::int32_t>::min(), 1,
       std::numeric_limits<std::int32_t>::max(), 8, 0, {0}},
  };
  const auto extreme = test_color_pat_bytes(
      3, 1, 1, "Extreme", "66666666-7777-8888-9999-aaaaaaaaaaaa",
      extreme_planes);
  append_pat_record(bytes, extreme);

  std::string error;
  const auto result = patchy::psd::read_pat(bytes, error);
  CHECK(result.has_value());
  CHECK(error.empty());
  CHECK(result->patterns.size() == 1U);
  CHECK(result->warnings.size() == 2U);
  CHECK(result->warnings[0].find("too many pixels") != std::string::npos);
  CHECK(result->warnings[1].find("rectangle is invalid") != std::string::npos);
}

void pat_extreme_plane_origin_samples_safely() {
  constexpr auto origin = std::numeric_limits<std::int32_t>::min();
  const std::vector<TestPatPlane> planes{
      {0, origin, origin, origin + 2, origin + 2, 8, 0, {10, 20, 30, 40}},
      {1, origin, origin, origin + 2, origin + 2, 8, 0, {50, 60, 70, 80}},
      {2, origin, origin, origin + 2, origin + 2, 8, 0, {90, 100, 110, 120}},
  };
  const auto bytes = test_color_pat_bytes(
      3, 2, 2, "Extreme Origin", "77777777-8888-9999-aaaa-bbbbbbbbbbbb", planes);
  std::string error;
  const auto result = patchy::psd::read_pat(bytes, error);
  CHECK(result.has_value());
  CHECK(error.empty());
  CHECK(result->warnings.empty());
  CHECK(result->patterns.size() == 1U);
  const auto* pixel = result->patterns.front().tile.pixel(0, 0);
  CHECK(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255);

  const auto indexed_bytes = test_indexed_pat_bytes(origin, origin, origin + 2, origin + 2);
  const auto indexed_result = patchy::psd::read_pat(indexed_bytes, error);
  CHECK(indexed_result.has_value());
  CHECK(error.empty());
  CHECK(indexed_result->warnings.empty());
  CHECK(indexed_result->patterns.size() == 1U);
  const auto* indexed_pixel = indexed_result->patterns.front().tile.pixel(0, 0);
  CHECK(indexed_pixel[0] == 0 && indexed_pixel[1] == 0 && indexed_pixel[2] == 0 &&
        indexed_pixel[3] == 255);
}

[[nodiscard]] const patchy::Layer* lmfx_fixture_layer(const patchy::Document& document,
                                                      std::string_view name) {
  for (const auto& layer : document.layers()) {
    if (layer.name() == name) {
      return &layer;
    }
  }
  return nullptr;
}

void psd_photoshop_lmfx_multi_effects_fixture_round_trips() {
  // PS 2026 re-save of Patchy's applied style presets: multi-instance effects
  // live in the 'lmfx' block (authoritative) with a single-instance
  // compatibility lfx2 beside it. The fixture's "Comic Pow" layer stacks two
  // strokes; pre-fix readers saw only the compat block and lost them.
  const auto path = patchy::test::committed_psd_fixture_path("photoshop-lmfx-multi-stroke.psd");
  CHECK(std::filesystem::exists(path));

  const auto layer_has_block = [](const patchy::Layer& layer, std::string_view key) {
    return std::any_of(layer.unknown_psd_blocks().begin(), layer.unknown_psd_blocks().end(),
                       [&key](const patchy::UnknownPsdBlock& block) { return block.key == key; });
  };
  const auto assert_document = [&](const patchy::Document& document) {
    const auto* comic = lmfx_fixture_layer(document, "Comic Pow");
    CHECK(comic != nullptr);
    const auto& style = comic->layer_style();
    CHECK(style.strokes.size() == 2U);
    CHECK(style.strokes[0].size == 3.0F);
    CHECK(style.strokes[0].position == patchy::LayerStrokePosition::Inside);
    CHECK(style.strokes[0].color.red == 226 && style.strokes[0].color.green == 6 &&
          style.strokes[0].color.blue == 44);
    CHECK(style.strokes[1].size == 6.0F);
    CHECK(style.strokes[1].position == patchy::LayerStrokePosition::Outside);
    CHECK(style.strokes[1].color.red == 255 && style.strokes[1].color.green == 255 &&
          style.strokes[1].color.blue == 255);
    CHECK(style.color_overlays.size() == 1U);
    CHECK(style.color_overlays.front().color.red == 255);
    CHECK(style.color_overlays.front().color.green == 205);
    CHECK(style.color_overlays.front().color.blue == 0);
    CHECK(style.drop_shadows.size() == 1U);
    CHECK(layer_has_block(*comic, "lmfx"));

    // Single-instance layers keep coming from the lfx2 path.
    const auto* adventure = lmfx_fixture_layer(document, "Adventure");
    CHECK(adventure != nullptr);
    CHECK(adventure->layer_style().gradient_fills.size() == 1U);
    CHECK(adventure->layer_style().strokes.size() == 1U);
    CHECK(adventure->layer_style().bevels.size() == 1U);
    CHECK(!layer_has_block(*adventure, "lmfx"));
  };

  auto document = patchy::psd::DocumentIo::read_file(path);
  assert_document(document);

  // An untouched resave preserves the raw lmfx block byte-for-byte and reads
  // back identically.
  const auto resaved_bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto resaved = patchy::psd::DocumentIo::read(resaved_bytes);
  assert_document(resaved);

  // Editing a style drops the stale lmfx (MainWindow's clear_layer_psd_style_source
  // contract) so the regenerated lfx2 is the only style source Photoshop sees.
  const auto* comic_before_edit = lmfx_fixture_layer(document, "Comic Pow");
  CHECK(comic_before_edit != nullptr);
  auto* comic = document.find_layer(comic_before_edit->id());
  CHECK(comic != nullptr);
  std::erase_if(comic->unknown_psd_blocks(), [](const patchy::UnknownPsdBlock& block) {
    return block.key == "lfx2" || block.key == "lrFX" || block.key == "plFX" ||
           block.key == "lmfx";
  });
  patchy::LayerStyle edited;
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.color = {12, 34, 56};
  shadow.opacity = 0.5F;
  shadow.distance = 4.0F;
  shadow.size = 2.0F;
  edited.drop_shadows.push_back(shadow);
  comic->layer_style() = edited;
  const auto edited_bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto reread = patchy::psd::DocumentIo::read(edited_bytes);
  const auto* edited_comic = lmfx_fixture_layer(reread, "Comic Pow");
  CHECK(edited_comic != nullptr);
  CHECK(!layer_has_block(*edited_comic, "lmfx"));
  CHECK(edited_comic->layer_style().strokes.empty());
  CHECK(edited_comic->layer_style().drop_shadows.size() == 1U);
  CHECK(edited_comic->layer_style().drop_shadows.front().color.red == 12);
  CHECK(edited_comic->layer_style().drop_shadows.front().color.blue == 56);
}

// --- .asl style library codec ------------------------------------------------

[[nodiscard]] patchy::LayerStyle asl_test_effects_style() {
  patchy::LayerStyle style;
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.color = {10, 20, 30};
  shadow.opacity = 0.8F;
  shadow.angle_degrees = 135.0F;
  shadow.distance = 7.0F;
  shadow.size = 4.0F;
  style.drop_shadows.push_back(shadow);
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.color = {200, 40, 90};
  stroke.opacity = 0.9F;
  stroke.size = 5.0F;
  stroke.position = patchy::LayerStrokePosition::Inside;
  style.strokes.push_back(stroke);
  return style;
}

[[nodiscard]] std::vector<patchy::psd::AslStyle> asl_test_styles() {
  std::vector<patchy::psd::AslStyle> styles;
  patchy::psd::AslStyle plain;
  plain.id = "11111111-2222-3333-4444-555555555555";
  plain.name = "Plain Effects";
  plain.style = asl_test_effects_style();
  styles.push_back(std::move(plain));

  patchy::psd::AslStyle rich;
  rich.id = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
  rich.name = "Rich Style";
  rich.style = asl_test_effects_style();
  patchy::LayerPatternOverlay overlay;
  overlay.enabled = true;
  overlay.pattern_id = "c4a11e00-000a-4b1d-9c3e-7a7c9e55b00a";  // built-in Bricks
  overlay.pattern_name = "Bricks";
  rich.style.pattern_overlays.push_back(overlay);
  patchy::psd::AslBlendSettings blend;
  blend.opacity = 63;
  blend.blend_mode = patchy::BlendMode::Multiply;
  blend.blend_if.channels[0].this_layer = {11, 37, 201, 239};
  blend.blend_if.channels[0].underlying_layer = {19, 53, 187, 227};
  blend.blend_if.channels[2].this_layer = {5, 35, 205, 235};
  rich.blend_settings = blend;
  styles.push_back(std::move(rich));
  return styles;
}

[[nodiscard]] std::vector<patchy::PatternResource> asl_test_patterns() {
  return {patchy::builtin_pattern_resource("c4a11e00-000a-4b1d-9c3e-7a7c9e55b00a")};
}

void asl_write_then_read_round_trips_styles_patterns_and_blend_options() {
  const auto styles = asl_test_styles();
  const auto patterns = asl_test_patterns();
  const auto bytes = patchy::psd::write_asl(styles, patterns);
  CHECK(!bytes.empty());
  // Deterministic output.
  CHECK(patchy::psd::write_asl(styles, patterns) == bytes);

  std::string error;
  const auto parsed = patchy::psd::read_asl(bytes, error);
  CHECK(parsed.has_value());
  CHECK(error.empty());
  CHECK(parsed->warnings.empty());
  CHECK(parsed->styles.size() == 2U);
  CHECK(parsed->patterns.size() == 1U);
  CHECK(parsed->patterns.front().id == patterns.front().id);
  CHECK(parsed->patterns.front().provenance == patchy::PatternProvenance::Authored);
  CHECK(parsed->patterns.front().tile.width() == patterns.front().tile.width());
  CHECK(std::equal(parsed->patterns.front().tile.data().begin(),
                   parsed->patterns.front().tile.data().end(),
                   patterns.front().tile.data().begin()));

  const auto& plain = parsed->styles[0];
  CHECK(plain.id == styles[0].id);
  CHECK(plain.name == styles[0].name);
  CHECK(!plain.blend_settings.has_value());
  CHECK(patchy::psd::photoshop_lfx2_layer_style_payload(plain.style) ==
        patchy::psd::photoshop_lfx2_layer_style_payload(styles[0].style));

  const auto& rich = parsed->styles[1];
  CHECK(rich.id == styles[1].id);
  CHECK(rich.name == styles[1].name);
  CHECK(rich.blend_settings.has_value());
  CHECK(*rich.blend_settings == *styles[1].blend_settings);
  CHECK(patchy::psd::photoshop_lfx2_layer_style_payload(rich.style) ==
        patchy::psd::photoshop_lfx2_layer_style_payload(styles[1].style));

  // A rewrite of the parsed result is byte-identical to the original file.
  CHECK(patchy::psd::write_asl(parsed->styles, parsed->patterns) == bytes);
}

void asl_writer_bytes_are_stable() {
  // Byte-stability canary (FNV-1a): .asl output is a user-facing interchange
  // format; re-pin only for deliberate format changes, never to make a
  // refactor pass (the failure output prints the hash).
  const auto bytes = patchy::psd::write_asl(asl_test_styles(), asl_test_patterns());
  const auto hash = fnv1a_hash_bytes(bytes);
  constexpr std::uint64_t kExpected = 0x11404e42246b7a3fULL;
  if (hash != kExpected) {
    std::cout << "asl writer hash: 0x" << std::hex << hash << std::dec << " size " << bytes.size()
              << "\n";
  }
  CHECK(hash == kExpected);
}

void asl_reader_reads_photoshop_blend_options_fixture() {
  // PS 2026-authored single-style export ("Patchy BO Probe"): drop shadow +
  // blending options captured from the Blend If fixture layer at opacity 63%,
  // fill 77%, Multiply. Pins the calibrated blendOptions layout.
  const auto path =
      patchy::test::committed_format_fixture_path("asl", "photoshop-style-blend-options.asl");
  CHECK(std::filesystem::exists(path));
  std::ifstream input(path, std::ios::binary);
  const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                        std::istreambuf_iterator<char>());
  std::string error;
  const auto parsed = patchy::psd::read_asl(bytes, error);
  CHECK(parsed.has_value());
  CHECK(error.empty());
  CHECK(parsed->styles.size() == 1U);
  CHECK(parsed->patterns.empty());
  const auto& style = parsed->styles.front();
  CHECK(style.name == "Patchy BO Probe");
  CHECK(style.id == "13761fc8-b0af-4e47-b9ce-354a5af46a2d");
  CHECK(style.style.effects_visible);
  CHECK(style.style.drop_shadows.size() == 1U);
  const auto& shadow = style.style.drop_shadows.front();
  CHECK(shadow.enabled);
  CHECK(shadow.blend_mode == patchy::BlendMode::Multiply);
  CHECK(std::abs(shadow.opacity - 0.75F) < 0.001F);
  CHECK(shadow.distance == 5.0F);
  CHECK(shadow.size == 5.0F);

  CHECK(style.blend_settings.has_value());
  CHECK(style.blend_settings->opacity == 63);
  CHECK(style.blend_settings->blend_mode == patchy::BlendMode::Multiply);
  const auto& blend_if = style.blend_settings->blend_if;
  // The values match the pinned photoshop-blend-if-4b-roundtrip.psd normal layer.
  CHECK(blend_if.channels[0].this_layer == (patchy::BlendIfThresholds{11, 37, 201, 239}));
  CHECK(blend_if.channels[0].underlying_layer == (patchy::BlendIfThresholds{19, 53, 187, 227}));
  CHECK(blend_if.channels[1].this_layer == (patchy::BlendIfThresholds{3, 33, 203, 233}));
  CHECK(blend_if.channels[1].underlying_layer == (patchy::BlendIfThresholds{13, 43, 193, 223}));
  CHECK(blend_if.channels[2].this_layer == (patchy::BlendIfThresholds{5, 35, 205, 235}));
  CHECK(blend_if.channels[2].underlying_layer == (patchy::BlendIfThresholds{15, 45, 195, 225}));
  CHECK(blend_if.channels[3].this_layer == (patchy::BlendIfThresholds{7, 37, 207, 237}));
  CHECK(blend_if.channels[3].underlying_layer == (patchy::BlendIfThresholds{17, 47, 197, 227}));

  // The unmodeled 77% fill opacity is reported, not silently dropped.
  auto fill_warning = false;
  for (const auto& warning : parsed->warnings) {
    fill_warning = fill_warning || warning.find("Fill opacity 77%") != std::string::npos;
  }
  CHECK(fill_warning);
}

void asl_reader_skips_damaged_records_and_rejects_bad_files() {
  const auto styles = asl_test_styles();
  const auto bytes = patchy::psd::write_asl(styles, {});

  const auto read_u32_at = [&bytes](std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3U]);
  };
  CHECK(read_u32_at(8U) == 0U);   // no patterns section
  CHECK(read_u32_at(12U) == 2U);  // two styles
  const auto first_record_length = read_u32_at(16U);

  // Truncation inside the second record keeps the decoded first style.
  std::string error;
  const auto truncated = std::vector<std::uint8_t>(
      bytes.begin(),
      bytes.begin() + static_cast<std::ptrdiff_t>(20U + first_record_length + 7U));
  const auto partial = patchy::psd::read_asl(truncated, error);
  CHECK(partial.has_value());
  CHECK(partial->styles.size() == 1U);
  CHECK(partial->styles.front().name == "Plain Effects");
  CHECK(!partial->warnings.empty());

  // A wrong signature is rejected.
  auto bad_signature = bytes;
  bad_signature[2] = 'X';
  CHECK(!patchy::psd::read_asl(bad_signature, error).has_value());
  CHECK(error == "Not a Photoshop ASL file");

  // Unsupported container versions are rejected.
  auto bad_version = bytes;
  bad_version[1] = 9;
  CHECK(!patchy::psd::read_asl(bad_version, error).has_value());
  CHECK(error.find("Unsupported ASL version") != std::string::npos);

  // An unsafe style count is rejected.
  auto huge_count = bytes;
  huge_count[12] = 0x7F;
  CHECK(!patchy::psd::read_asl(huge_count, error).has_value());
  CHECK(error.find("style count exceeds") != std::string::npos);

  // Damage inside every record leaves nothing decodable.
  auto no_styles = std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + 18);
  CHECK(!patchy::psd::read_asl(no_styles, error).has_value());
  CHECK(!error.empty());
}

void style_presets_have_stable_ids_and_recipes() {
  const auto presets = patchy::builtin_style_presets();
  CHECK(presets.size() == 39U);
  std::size_t text_count = 0;
  std::size_t basics_count = 0;
  std::size_t materials_count = 0;
  for (const auto& preset : presets) {
    CHECK(patchy::find_builtin_style_preset(preset.id) == &preset);
    // Ids are unique.
    for (const auto& other : presets) {
      CHECK(&preset == &other || std::string_view(preset.id) != other.id);
    }
    const auto folder = std::string_view(preset.english_folder);
    CHECK(folder == "Text" || folder == "Basics" || folder == "Materials");
    text_count += folder == "Text" ? 1U : 0U;
    basics_count += folder == "Basics" ? 1U : 0U;
    materials_count += folder == "Materials" ? 1U : 0U;
    // Text/Basics shipped with defaults version 1, Materials with version 2.
    CHECK(preset.introduced_version == (folder == "Materials" ? 2 : 1));

    const auto style = patchy::builtin_style_preset_style(preset.id);
    const auto effect_count = style.drop_shadows.size() + style.inner_shadows.size() +
                              style.outer_glows.size() + style.inner_glows.size() +
                              style.color_overlays.size() + style.gradient_fills.size() +
                              style.pattern_overlays.size() + style.strokes.size() +
                              style.bevels.size() + style.satins.size();
    CHECK(effect_count > 0U);

    // Every referenced pattern resolves among the bundled pattern presets
    // (code-generated or photo texture), so applying a preset can always
    // materialize its tiles.
    std::vector<std::string> referenced;
    patchy::collect_referenced_pattern_ids(style, referenced);
    for (const auto& id : referenced) {
      CHECK(patchy::find_builtin_pattern_preset(id) != nullptr ||
            patchy::find_photo_pattern_preset(id) != nullptr);
    }

    // Every recipe survives the native lfx2 round trip losslessly (write ->
    // parse -> write is byte-identical), so presets applied to layers reopen
    // from PSDs exactly as authored.
    const auto payload = patchy::psd::photoshop_lfx2_layer_style_payload(style);
    patchy::psd::BigEndianReader reader(
        std::span<const std::uint8_t>(payload).subspan(8));
    const auto descriptor = patchy::psd::read_descriptor(reader);
    const auto reparsed = patchy::psd::layer_style_from_lefx_descriptor(descriptor, nullptr);
    CHECK(patchy::psd::photoshop_lfx2_layer_style_payload(reparsed) == payload);
  }
  CHECK(text_count == 20U);
  CHECK(basics_count == 6U);
  CHECK(materials_count == 13U);
  CHECK(patchy::builtin_style_preset_style("not-a-real-id").drop_shadows.empty());
}

void asl_reader_reads_photoshop_shipped_styles_if_available() {
  const auto path = patchy::test::local_format_fixture_path("asl", "Abstract Styles.asl");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local ASL fixture missing: " << path.string() << '\n';
    return;
  }
  std::ifstream input(path, std::ios::binary);
  const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                        std::istreambuf_iterator<char>());
  std::string error;
  const auto parsed = patchy::psd::read_asl(bytes, error);
  CHECK(parsed.has_value());
  CHECK(error.empty());
  CHECK(parsed->styles.size() == 6U);
  CHECK(parsed->patterns.size() == 6U);
  // ZString display names resolve.
  CHECK(parsed->styles.front().name == "White Grid on Orange");
  for (const auto& style : parsed->styles) {
    CHECK(!style.id.empty());
    CHECK(!style.style.pattern_overlays.empty() || !style.style.color_overlays.empty());
  }
}

void pat_attempt_budgets_include_failed_compressed_records() {
  {
    auto bytes = test_raw_rgb_pat_bytes();
    write_test_u32_at(bytes, 6U, 4U);

    // Each written plane expands to just under eight million samples. Two
    // malformed CMYK+alpha records fit under the 80 Mi-sample attempt budget;
    // their empty RLE data then fails in the decoder. The third record must be
    // stopped by the cumulative preflight rather than attempting decompression.
    std::vector<TestPatPlane> planes;
    for (const auto slot : {0U, 1U, 2U, 3U, 25U}) {
      planes.push_back({slot, 0, 0, 1024, 8191, 8, 1, {}});
    }
    for (std::uint32_t index = 0; index < 3U; ++index) {
      const auto id = "aaaaaaaa-0000-0000-0000-" +
                      std::string(11U, '0') + static_cast<char>('1' + index);
      const auto record = test_color_pat_bytes(
          4, 1, 1, "Plane attempt " + std::to_string(index + 1U), id, planes);
      append_pat_record(bytes, record);
    }

    std::string error;
    const auto result = patchy::psd::read_pat(bytes, error);
    CHECK(result.has_value());
    CHECK(error.empty());
    CHECK(result->patterns.size() == 1U);
    CHECK(result->warnings.size() == 3U);
    CHECK(result->warnings[0].find("channel data could not be decoded") != std::string::npos);
    CHECK(result->warnings[1].find("channel data could not be decoded") != std::string::npos);
    CHECK(result->warnings[2].find("cumulative plane sample limit") != std::string::npos);
  }

  {
    auto bytes = test_raw_rgb_pat_bytes();
    write_test_u32_at(bytes, 6U, 3U);
    const std::vector<TestPatPlane> planes{
        {0, 0, 0, 1, 1, 8, 1, {}},
    };
    for (std::uint32_t index = 0; index < 2U; ++index) {
      const auto id = "bbbbbbbb-0000-0000-0000-" +
                      std::string(11U, '0') + static_cast<char>('1' + index);
      const auto record = test_color_pat_bytes(
          3, 4096, 2048, "Pixel attempt " + std::to_string(index + 1U), id, planes);
      append_pat_record(bytes, record);
    }

    std::string error;
    const auto result = patchy::psd::read_pat(bytes, error);
    CHECK(result.has_value());
    CHECK(error.empty());
    CHECK(result->patterns.size() == 1U);
    CHECK(result->warnings.size() == 2U);
    CHECK(result->warnings[0].find("channel data could not be decoded") != std::string::npos);
    CHECK(result->warnings[1].find("total pixel limit") != std::string::npos);
  }
}

void psd_pattern_unused_vma_slot_is_not_decoded() {
  const std::vector<TestPatPlane> planes{
      {0, 0, 0, 2, 2, 8, 0, {10, 20, 30, 40}},
      {1, 0, 0, 2, 2, 8, 0, {50, 60, 70, 80}},
      {2, 0, 0, 2, 2, 8, 0, {90, 100, 110, 120}},
      // Slot 4 is neither an RGB channel nor sheet alpha. Its malformed RLE
      // body must be length-checked and skipped without decompression.
      {4, 0, 0, 1024, 8191, 8, 1, {}},
  };
  const auto pat = test_color_pat_bytes(
      3, 2, 2, "Unused slot", "cccccccc-0000-0000-0000-000000000001", planes);
  const auto payload = pat_record_as_patterns_block(pat);
  const auto patterns = patchy::psd::parse_patterns_block(payload, nullptr);
  CHECK(patterns.size() == 1U);
  CHECK(patterns.front().id == "cccccccc-0000-0000-0000-000000000001");
  const auto* pixel = patterns.front().tile.pixel(1, 1);
  CHECK(pixel[0] == 40 && pixel[1] == 80 && pixel[2] == 120 && pixel[3] == 255);
}

void psd_pattern_vma_cannot_consume_the_next_record() {
  auto malformed_pat = test_raw_rgb_pat_bytes(
      "88888888-9999-aaaa-bbbb-cccccccccccc");
  const auto vma_length_offset = test_pat_vma_length_offset(malformed_pat);
  patchy::psd::BigEndianReader length_reader(
      std::span<const std::uint8_t>(malformed_pat).subspan(vma_length_offset, 4U));
  const auto vma_length = length_reader.read_u32();
  write_test_u32_at(malformed_pat, vma_length_offset, vma_length + 4U);

  auto payload = pat_record_as_patterns_block(malformed_pat);
  const auto valid_pat = test_raw_rgb_pat_bytes(
      "99999999-aaaa-bbbb-cccc-dddddddddddd");
  const auto valid_block = pat_record_as_patterns_block(valid_pat);
  payload.insert(payload.end(), valid_block.begin(), valid_block.end());

  const auto patterns = patchy::psd::parse_patterns_block(payload, nullptr);
  CHECK(patterns.size() == 1U);
  CHECK(patterns.front().id == "99999999-aaaa-bbbb-cccc-dddddddddddd");
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

}  // namespace

std::vector<patchy::test::TestCase> pat_asl_abr_tests() {
  return {
      {"pat_hue_fixture_decodes_packbits_and_alpha", pat_hue_fixture_decodes_packbits_and_alpha},
      {"pat_raw_rgb_record_imports_without_psd_length_wrapper",
       pat_raw_rgb_record_imports_without_psd_length_wrapper},
      {"pat_indexed_footer_applies_transparent_palette_index",
       pat_indexed_footer_applies_transparent_palette_index},
      {"pat_rejects_wrong_magic_version_truncation_and_empty_files",
       pat_rejects_wrong_magic_version_truncation_and_empty_files},
      {"pat_gray16_and_cmyk_records_decode", pat_gray16_and_cmyk_records_decode},
      {"pat_invalid_utf8_id_is_replaced", pat_invalid_utf8_id_is_replaced},
      {"pat_trailing_hierarchy_is_ignored", pat_trailing_hierarchy_is_ignored},
      {"pat_bounds_compressed_expansion_and_extreme_rectangles",
       pat_bounds_compressed_expansion_and_extreme_rectangles},
      {"pat_extreme_plane_origin_samples_safely", pat_extreme_plane_origin_samples_safely},
      {"psd_photoshop_lmfx_multi_effects_fixture_round_trips",
       psd_photoshop_lmfx_multi_effects_fixture_round_trips},
      {"asl_write_then_read_round_trips_styles_patterns_and_blend_options",
       asl_write_then_read_round_trips_styles_patterns_and_blend_options},
      {"asl_writer_bytes_are_stable", asl_writer_bytes_are_stable},
      {"asl_reader_reads_photoshop_blend_options_fixture",
       asl_reader_reads_photoshop_blend_options_fixture},
      {"asl_reader_skips_damaged_records_and_rejects_bad_files",
       asl_reader_skips_damaged_records_and_rejects_bad_files},
      {"style_presets_have_stable_ids_and_recipes", style_presets_have_stable_ids_and_recipes},
      {"asl_reader_reads_photoshop_shipped_styles_if_available",
       asl_reader_reads_photoshop_shipped_styles_if_available},
      {"pat_attempt_budgets_include_failed_compressed_records",
       pat_attempt_budgets_include_failed_compressed_records},
      {"psd_pattern_unused_vma_slot_is_not_decoded",
       psd_pattern_unused_vma_slot_is_not_decoded},
      {"psd_pattern_vma_cannot_consume_the_next_record",
       psd_pattern_vma_cannot_consume_the_next_record},
      {"abr_v6_fixture_parses_brushes_names_and_spacing", abr_v6_fixture_parses_brushes_names_and_spacing},
      {"abr_dynamics_fixture_extracts_shape_and_scatter", abr_dynamics_fixture_extracts_shape_and_scatter},
      {"abr_myer_brushes_have_default_dynamics", abr_myer_brushes_have_default_dynamics},
      {"abr_v6_desc_controls_import", abr_v6_desc_controls_import},
      {"abr_v1_parses_sampled_brush_and_skips_computed", abr_v1_parses_sampled_brush_and_skips_computed},
      {"abr_v2_parses_named_rle_and_16bit_brushes", abr_v2_parses_named_rle_and_16bit_brushes},
      {"abr_rejects_corrupt_truncated_and_empty_files", abr_rejects_corrupt_truncated_and_empty_files},
  };
}
