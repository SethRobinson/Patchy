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
#include "smart_filter_descriptors_test_support.hpp"
#include "test_groups.hpp"

namespace {

using patchy::test::require_box_blur_filter;
using patchy::test::require_dust_and_scratches_filter;
using patchy::test::require_emboss_filter;
using patchy::test::require_gaussian_filter;
using patchy::test::require_high_pass_filter;
using patchy::test::require_layer_named;
using patchy::test::require_median_filter;
using patchy::test::require_mosaic_filter;
using patchy::test::require_plastic_wrap_filter;
using patchy::test::require_smart_filter_stack;
using patchy::test::require_surface_blur_filter;
using patchy::test::solid_rgba;
using patchy::test::test_dust_and_scratches_smart_filter_stack;
using patchy::test::test_gaussian_smart_filter_stack;
using patchy::test::test_high_pass_smart_filter_stack;
using patchy::test::test_median_smart_filter_stack;
using patchy::test::test_surface_blur_smart_filter_stack;

void smart_filter_authored_effects_record_round_trips_and_mutates() {
  const std::string placed_uuid = "01234567-89ab-cdef-8123-456789abcdef";
  const patchy::Rect document_bounds{0, 0, 4, 3};
  auto source = solid_rgba(2, 2, 10, 20, 30, 255);
  source.pixel(1, 0)[0] = 40;
  source.pixel(0, 1)[1] = 50;
  source.pixel(1, 1)[2] = 60;
  source.pixel(1, 1)[3] = 128;
  const patchy::Rect source_bounds{1, 1, 2, 2};

  patchy::SmartFilterMask mask;
  mask.bounds = document_bounds;
  mask.pixels = patchy::PixelBuffer(4, 3, patchy::PixelFormat::gray8());
  const std::array<std::uint8_t, 12> mask_samples{
      0, 32, 64, 96, 128, 160, 192, 224, 255, 192, 128, 64};
  std::copy(mask_samples.begin(), mask_samples.end(), mask.pixels.data().begin());
  mask.default_color = 17;
  mask.extend_with_white = false;

  const auto authored = patchy::psd::author_filter_effects_record(
      placed_uuid, document_bounds, source, source_bounds, mask);
  CHECK(authored.has_value());
  CHECK(authored->placed_uuid == placed_uuid);
  CHECK(authored->record_version == 1U);
  CHECK(authored->cache_layout_valid);
  CHECK(authored->cache_bounds.x == 0 && authored->cache_bounds.y == 0);
  CHECK(authored->cache_bounds.width == 4 && authored->cache_bounds.height == 3);
  CHECK(authored->cache_depth == 8U);
  CHECK(authored->cache_max_channels == 24U);
  CHECK(authored->mask_present && authored->mask_decoded && authored->mask.has_value());
  CHECK(authored->mask->bounds.x == 0 && authored->mask->bounds.y == 0);
  CHECK(authored->mask->bounds.width == 4 && authored->mask->bounds.height == 3);
  CHECK(authored->mask->samples != nullptr);
  CHECK(authored->mask->samples->size() == mask_samples.size());
  CHECK(std::equal(mask_samples.begin(), mask_samples.end(),
                   authored->mask->samples->begin()));

  // Pin the Photoshop cache-slot shape: RGB at slots 0-2, alpha at the final
  // sheet-mask slot (25), and every intervening native channel absent.
  const auto raw_body = patchy::psd::raw_filter_effects_record_body(*authored);
  patchy::psd::BigEndianReader record_reader(raw_body);
  const auto identifier_length = record_reader.read_u8();
  CHECK(identifier_length == placed_uuid.size());
  const auto identifier_bytes = record_reader.read_bytes(identifier_length);
  CHECK(std::string(identifier_bytes.begin(), identifier_bytes.end()) == placed_uuid);
  CHECK(record_reader.read_u32() == 1U);
  const auto cache_length = record_reader.read_u64();
  CHECK(cache_length <= record_reader.remaining());
  const auto cache_start = record_reader.position();
  patchy::psd::BigEndianReader cache_reader(raw_body.subspan(
      cache_start, static_cast<std::size_t>(cache_length)));
  CHECK(static_cast<std::int32_t>(cache_reader.read_u32()) == document_bounds.y);
  CHECK(static_cast<std::int32_t>(cache_reader.read_u32()) == document_bounds.x);
  CHECK(static_cast<std::int32_t>(cache_reader.read_u32()) ==
        document_bounds.y + document_bounds.height);
  CHECK(static_cast<std::int32_t>(cache_reader.read_u32()) ==
        document_bounds.x + document_bounds.width);
  CHECK(cache_reader.read_u32() == 8U);
  CHECK(cache_reader.read_u32() == 24U);
  for (std::uint32_t slot = 0; slot < 26U; ++slot) {
    const auto written = cache_reader.read_u32();
    const bool expected = slot <= 2U || slot == 25U;
    CHECK(written == (expected ? 1U : 0U));
    if (written != 0U) {
      const auto plane_length = cache_reader.read_u64();
      CHECK(plane_length > 2U && plane_length <= cache_reader.remaining());
      cache_reader.skip(static_cast<std::size_t>(plane_length));
    }
  }
  CHECK(cache_reader.remaining() == 0U);

  patchy::SmartFilterEffectsStore store;
  CHECK(store.upsert_authored(*authored));
  CHECK(store.blocks.size() == 1U);
  CHECK(store.blocks.front().key == "FEid" && store.blocks.front().version == 3U);
  CHECK(store.blocks.front().records.size() == 1U);
  const auto serialized = patchy::psd::serialize_filter_effects_block(store.blocks.front());
  const auto parsed_block = patchy::psd::parse_filter_effects_block("FEid", serialized);
  CHECK(!parsed_block.opaque && parsed_block.records.size() == 1U);
  CHECK(parsed_block.records.front().semantic_supported());
  CHECK(parsed_block.records.front().mask.has_value());
  CHECK(parsed_block.records.front().mask->samples->size() == mask_samples.size());
  CHECK(std::equal(mask_samples.begin(), mask_samples.end(),
                   parsed_block.records.front().mask->samples->begin()));

  auto replacement_mask = mask;
  replacement_mask.pixels.clear(7);
  const auto replacement = patchy::psd::author_filter_effects_record(
      placed_uuid, document_bounds, source, source_bounds, replacement_mask);
  CHECK(replacement.has_value());
  CHECK(store.upsert_authored(*replacement));
  CHECK(store.blocks.size() == 1U && store.blocks.front().records.size() == 1U);
  const auto* replaced = store.find_unique(placed_uuid);
  CHECK(replaced != nullptr && replaced->mask.has_value());
  CHECK(replaced->mask->samples->size() == mask_samples.size());
  CHECK(std::all_of(replaced->mask->samples->begin(), replaced->mask->samples->end(),
                    [](std::uint8_t sample) { return sample == 7U; }));
  CHECK(store.remove(placed_uuid));
  CHECK(store.empty());
  CHECK(!store.remove(placed_uuid));
}

void smart_filter_effects_author_rejects_oversized_bounds() {
  const std::string placed_uuid = "01234567-89ab-cdef-8123-456789abcdef";
  const auto source = solid_rgba(1, 1, 10, 20, 30, 255);
  const patchy::Rect source_bounds{0, 0, 1, 1};
  const patchy::SmartFilterMask mask;
  const auto rejected = [&](patchy::Rect document_bounds) {
    return !patchy::psd::author_filter_effects_record(
                placed_uuid, document_bounds, source, source_bounds, mask)
                .has_value();
  };

  CHECK(rejected(patchy::Rect{0, 0, 300001, 1}));
  CHECK(rejected(patchy::Rect{0, 0, 1, 300001}));
  // 8192 squared is exactly the 64 Mi-pixel edit ceiling. One extra row's
  // worth of pixels must fail before any cache or mask allocation is attempted.
  CHECK(rejected(patchy::Rect{0, 0, 8193, 8192}));
}

void smart_filter_effects_mask_replacement_preserves_native_structure() {
  const std::string placed_a = "11111111-2222-3333-8444-555555555555";
  const std::string placed_b = "66666666-7777-4888-9999-aaaaaaaaaaaa";
  const patchy::Rect bounds{-2, 3, 3, 2};
  auto source = solid_rgba(3, 2, 10, 20, 30, 255);
  source.pixel(1, 0)[3] = 128U;

  patchy::SmartFilterMask initial_mask;
  initial_mask.bounds = bounds;
  initial_mask.pixels = patchy::PixelBuffer(3, 2, patchy::PixelFormat::gray8());
  const std::array<std::uint8_t, 6> initial_samples{0, 0, 255, 255, 0, 255};
  std::copy(initial_samples.begin(), initial_samples.end(),
            initial_mask.pixels.data().begin());
  const auto authored_a = patchy::psd::author_filter_effects_record(
      placed_a, bounds, source, bounds, initial_mask);
  const auto authored_b = patchy::psd::author_filter_effects_record(
      placed_b, bounds, source, bounds, initial_mask);
  CHECK(authored_a.has_value() && authored_b.has_value());

  // Exercise a preserved Photoshop dialect that differs from Patchy's authored
  // FEid-v3 default. The long-length form lives on the enclosing tagged block.
  patchy::SmartFilterEffectsBlock source_block;
  source_block.key = "FXid";
  source_block.version = 2U;
  source_block.long_length = true;
  source_block.original_global_index = 91U;
  source_block.records = {*authored_a, *authored_b};
  const auto source_payload =
      patchy::psd::serialize_filter_effects_block(source_block);
  auto parsed = patchy::psd::parse_filter_effects_block(
      "FXid", source_payload, true, 91U);
  CHECK(!parsed.opaque && parsed.records.size() == 2U);
  patchy::SmartFilterEffectsStore store;
  store.add_block(std::move(parsed));

  const auto cache_prefix = [](const patchy::SmartFilterEffectsRecord& record) {
    const auto body = patchy::psd::raw_filter_effects_record_body(record);
    patchy::psd::BigEndianReader reader(body);
    const auto id_length = reader.read_u8();
    reader.skip(id_length);
    CHECK(reader.read_u32() == 1U);
    const auto cache_length = reader.read_u64();
    CHECK(cache_length <= reader.remaining());
    reader.skip(static_cast<std::size_t>(cache_length));
    return std::vector<std::uint8_t>(
        body.begin(),
        body.begin() + static_cast<std::ptrdiff_t>(reader.position()));
  };
  const auto prefix_before = cache_prefix(store.blocks.front().records[0]);
  const auto other_body_before = std::vector<std::uint8_t>(
      patchy::psd::raw_filter_effects_record_body(
          store.blocks.front().records[1])
          .begin(),
      patchy::psd::raw_filter_effects_record_body(
          store.blocks.front().records[1])
          .end());

  patchy::SmartFilterMask fractional;
  fractional.bounds = bounds;
  fractional.pixels = patchy::PixelBuffer(3, 2, patchy::PixelFormat::gray8());
  const std::array<std::uint8_t, 6> fractional_samples{0, 32, 96, 160, 224,
                                                       255};
  std::copy(fractional_samples.begin(), fractional_samples.end(),
            fractional.pixels.data().begin());
  fractional.enabled = false;
  fractional.linked = false;
  fractional.extend_with_white = false;
  CHECK(patchy::psd::replace_filter_effects_mask(store, placed_a,
                                                  fractional));
  CHECK(store.blocks.size() == 1U);
  const auto& mutated_block = store.blocks.front();
  CHECK(mutated_block.key == "FXid" && mutated_block.version == 2U);
  CHECK(mutated_block.long_length && mutated_block.original_global_index == 91U);
  CHECK(mutated_block.records.size() == 2U);
  CHECK(mutated_block.records[0].placed_uuid == placed_a);
  CHECK(mutated_block.records[1].placed_uuid == placed_b);
  CHECK(cache_prefix(mutated_block.records[0]) == prefix_before);
  CHECK(std::equal(
      other_body_before.begin(), other_body_before.end(),
      patchy::psd::raw_filter_effects_record_body(mutated_block.records[1])
          .begin(),
      patchy::psd::raw_filter_effects_record_body(mutated_block.records[1])
          .end()));
  CHECK(mutated_block.records[0].mask.has_value());
  CHECK(std::equal(fractional_samples.begin(), fractional_samples.end(),
                   mutated_block.records[0].mask->samples->begin()));

  const auto fractional_payload =
      patchy::psd::serialize_filter_effects_block(mutated_block);
  CHECK((fractional_payload.size() % 4U) == 0U);
  const auto round_trip = patchy::psd::parse_filter_effects_block(
      "FXid", fractional_payload, true, 91U);
  CHECK(!round_trip.opaque && round_trip.records.size() == 2U);
  CHECK(round_trip.long_length && round_trip.version == 2U);
  CHECK(round_trip.records[0].semantic_supported());
  CHECK(round_trip.records[0].mask.has_value());
  CHECK(std::equal(fractional_samples.begin(), fractional_samples.end(),
                   round_trip.records[0].mask->samples->begin()));
  CHECK(patchy::psd::serialize_filter_effects_block(round_trip) ==
        fractional_payload);

  // Enabled/link/extension are SoLd descriptor leaves, not FEid bytes. A
  // flag-only update must be an exact no-op on the native cache block.
  auto flags_only = fractional;
  flags_only.enabled = true;
  flags_only.linked = true;
  flags_only.extend_with_white = true;
  CHECK(patchy::psd::replace_filter_effects_mask(store, placed_a,
                                                  flags_only));
  CHECK(patchy::psd::serialize_filter_effects_block(store.blocks.front()) ==
        fractional_payload);

  // A subsequent hard mask uses the same bounded replacement path.
  patchy::SmartFilterMask hard = fractional;
  const std::array<std::uint8_t, 6> hard_samples{255, 0, 255, 0, 255, 0};
  std::copy(hard_samples.begin(), hard_samples.end(),
            hard.pixels.data().begin());
  hard.enabled = false;
  CHECK(patchy::psd::replace_filter_effects_mask(store, placed_a, hard));
  const auto hard_payload =
      patchy::psd::serialize_filter_effects_block(store.blocks.front());
  const auto hard_round_trip = patchy::psd::parse_filter_effects_block(
      "FXid", hard_payload, true, 91U);
  CHECK(hard_round_trip.records[0].mask.has_value());
  CHECK(std::equal(hard_samples.begin(), hard_samples.end(),
                   hard_round_trip.records[0].mask->samples->begin()));
  CHECK(cache_prefix(hard_round_trip.records[0]) == prefix_before);
}

void smart_filter_effects_mask_replacement_adds_removes_and_fails_closed() {
  const std::string placed = "01234567-89ab-cdef-8123-456789abcdef";
  const patchy::Rect bounds{0, 0, 2, 1};
  const auto source = solid_rgba(2, 1, 10, 20, 30, 255);
  patchy::SmartFilterMask authored_mask;
  authored_mask.bounds = bounds;
  authored_mask.pixels = patchy::PixelBuffer(2, 1, patchy::PixelFormat::gray8());
  authored_mask.pixels.clear(255U);
  const auto authored = patchy::psd::author_filter_effects_record(
      placed, bounds, source, bounds, authored_mask);
  CHECK(authored.has_value());

  const auto record_prefix = [](const patchy::SmartFilterEffectsRecord& record) {
    const auto body = patchy::psd::raw_filter_effects_record_body(record);
    patchy::psd::BigEndianReader reader(body);
    const auto id_length = reader.read_u8();
    reader.skip(id_length);
    CHECK(reader.read_u32() == 1U);
    const auto cache_length = reader.read_u64();
    CHECK(cache_length <= reader.remaining());
    reader.skip(static_cast<std::size_t>(cache_length));
    return std::vector<std::uint8_t>(
        body.begin(),
        body.begin() + static_cast<std::ptrdiff_t>(reader.position()));
  };
  const auto prefix = record_prefix(*authored);
  const auto wrap_body = [](std::span<const std::uint8_t> body,
                            std::uint32_t version = 3U) {
    patchy::psd::BigEndianWriter writer;
    writer.write_u32(version);
    writer.write_u64(static_cast<std::uint64_t>(body.size()));
    writer.write_bytes(body);
    while ((writer.bytes().size() % 4U) != 0U) {
      writer.write_u8(0U);
    }
    return writer.bytes();
  };
  const auto make_no_mask_store = [&]() {
    patchy::SmartFilterEffectsStore result;
    result.add_block(patchy::psd::parse_filter_effects_block(
        "FEid", wrap_body(prefix)));
    return result;
  };

  auto store = make_no_mask_store();
  const auto* no_mask = store.find_unique(placed);
  CHECK(no_mask != nullptr && no_mask->semantic_supported());
  CHECK(!no_mask->mask_present && !no_mask->mask.has_value());

  patchy::SmartFilterMask hard;
  hard.bounds = bounds;
  hard.pixels = patchy::PixelBuffer(2, 1, patchy::PixelFormat::gray8());
  hard.pixels.pixel(0, 0)[0] = 0U;
  hard.pixels.pixel(1, 0)[0] = 255U;
  CHECK(patchy::psd::replace_filter_effects_mask(store, placed, hard));
  const auto with_mask_payload =
      patchy::psd::serialize_filter_effects_block(store.blocks.front());
  const auto with_mask = patchy::psd::parse_filter_effects_block(
      "FEid", with_mask_payload);
  CHECK(with_mask.records.front().semantic_supported());
  CHECK(with_mask.records.front().mask_present);
  CHECK(with_mask.records.front().mask->samples->at(0) == 0U);
  CHECK(with_mask.records.front().mask->samples->at(1) == 255U);
  CHECK(record_prefix(with_mask.records.front()) == prefix);

  // Empty pixels intentionally remove the entire optional tail rather than
  // writing the alternate one-byte zero-presence form.
  patchy::SmartFilterMask none;
  CHECK(patchy::psd::replace_filter_effects_mask(store, placed, none));
  const auto without_mask_payload =
      patchy::psd::serialize_filter_effects_block(store.blocks.front());
  const auto without_mask = patchy::psd::parse_filter_effects_block(
      "FEid", without_mask_payload);
  CHECK(without_mask.records.front().semantic_supported());
  CHECK(!without_mask.records.front().mask_present);
  const auto without_mask_body =
      patchy::psd::raw_filter_effects_record_body(without_mask.records.front());
  CHECK(std::equal(prefix.begin(), prefix.end(), without_mask_body.begin(),
                   without_mask_body.end()));

  // Invalid gray buffers fail before touching the store.
  auto invalid_store = make_no_mask_store();
  const auto invalid_before = patchy::psd::serialize_filter_effects_block(
      invalid_store.blocks.front());
  auto invalid_format = hard;
  invalid_format.pixels = patchy::PixelBuffer(2, 1, patchy::PixelFormat::rgba8());
  CHECK(!patchy::psd::replace_filter_effects_mask(invalid_store, placed,
                                                   invalid_format));
  auto invalid_bounds = hard;
  invalid_bounds.bounds.width = 1;
  CHECK(!patchy::psd::replace_filter_effects_mask(invalid_store, placed,
                                                   invalid_bounds));
  auto overflow_bounds = hard;
  overflow_bounds.bounds =
      patchy::Rect{std::numeric_limits<std::int32_t>::max(), 0, 2, 1};
  CHECK(!patchy::psd::replace_filter_effects_mask(invalid_store, placed,
                                                   overflow_bounds));
  CHECK(patchy::psd::serialize_filter_effects_block(
            invalid_store.blocks.front()) == invalid_before);

  // Duplicate associations, unsupported targets, and an opaque sibling block
  // all make the operation fail closed and retain the exact original bytes.
  patchy::psd::BigEndianWriter duplicate_writer;
  duplicate_writer.write_u32(3U);
  for (int copy = 0; copy < 2; ++copy) {
    duplicate_writer.write_u64(static_cast<std::uint64_t>(prefix.size()));
    duplicate_writer.write_bytes(prefix);
  }
  auto duplicate_payload = duplicate_writer.bytes();
  while ((duplicate_payload.size() % 4U) != 0U) {
    duplicate_payload.push_back(0U);
  }
  patchy::SmartFilterEffectsStore duplicate_store;
  duplicate_store.add_block(patchy::psd::parse_filter_effects_block(
      "FEid", duplicate_payload));
  CHECK(!patchy::psd::replace_filter_effects_mask(duplicate_store, placed,
                                                   hard));
  CHECK(patchy::psd::serialize_filter_effects_block(
            duplicate_store.blocks.front()) == duplicate_payload);

  auto unsupported_store = make_no_mask_store();
  unsupported_store.blocks.front().records.front().data_supported = false;
  const auto unsupported_before = patchy::psd::serialize_filter_effects_block(
      unsupported_store.blocks.front());
  CHECK(!patchy::psd::replace_filter_effects_mask(unsupported_store, placed,
                                                   hard));
  CHECK(patchy::psd::serialize_filter_effects_block(
            unsupported_store.blocks.front()) == unsupported_before);

  auto opaque_store = make_no_mask_store();
  patchy::psd::BigEndianWriter opaque_writer;
  opaque_writer.write_u32(99U);
  const auto opaque_payload = opaque_writer.bytes();
  opaque_store.add_block(patchy::psd::parse_filter_effects_block(
      "FXid", opaque_payload));
  const auto valid_before = patchy::psd::serialize_filter_effects_block(
      opaque_store.blocks.front());
  CHECK(!patchy::psd::replace_filter_effects_mask(opaque_store, placed, hard));
  CHECK(patchy::psd::serialize_filter_effects_block(
            opaque_store.blocks.front()) == valid_before);
  CHECK(patchy::psd::serialize_filter_effects_block(
            opaque_store.blocks.back()) == opaque_payload);
}

void smart_filter_canonical_gaussian_descriptor_authors_patches_and_removes() {
  patchy::SmartObjectPlacement placement;
  placement.uuid = "11111111-2222-3333-8444-555555555555";
  placement.transform = {10.0, 20.0, 42.0, 20.0, 42.0, 44.0, 10.0, 44.0};
  placement.width = 32.0;
  placement.height = 24.0;
  placement.resolution = 72.0;
  const std::string placed_uuid = "aaaaaaaa-bbbb-cccc-8ddd-eeeeeeeeeeee";

  auto stack = test_gaussian_smart_filter_stack(2.5);
  stack.valid_at_position = false;
  stack.mask.linked = false;
  stack.mask.extend_with_white = false;
  stack.entries.front().opacity = 0.37;
  stack.entries.front().blend_mode = patchy::BlendMode::Multiply;
  stack.entries.front().foreground = patchy::RgbColor{1, 2, 3};
  stack.entries.front().background = patchy::RgbColor{250, 249, 248};

  const auto descriptor_from_sold = [](std::span<const std::uint8_t> sold) {
    patchy::psd::BigEndianReader reader(sold);
    CHECK(patchy::psd::key_string(patchy::psd::read_signature(reader)) == "soLD");
    CHECK(reader.read_u32() == 4U);
    CHECK(reader.read_u32() == 16U);
    return patchy::psd::read_descriptor(reader);
  };
  const auto sold_from_descriptor = [](const patchy::psd::DescriptorObject& descriptor) {
    patchy::psd::BigEndianWriter writer;
    for (const char ch : {'s', 'o', 'L', 'D'}) {
      writer.write_u8(static_cast<std::uint8_t>(ch));
    }
    writer.write_u32(4U);
    writer.write_u32(16U);
    patchy::psd::write_descriptor(writer, descriptor);
    while ((writer.bytes().size() % 4U) != 0U) {
      writer.write_u8(0U);
    }
    return writer.bytes();
  };
  const auto key_shape = [](const patchy::psd::DescriptorObject& object) {
    std::vector<std::string> result;
    result.reserve(object.key_order.size());
    for (const auto& entry : object.key_order) {
      result.push_back(entry.key + (entry.long_form ? "+" : "-"));
    }
    return result;
  };
  const auto rename_descriptor_key = [](patchy::psd::DescriptorObject& object,
                                        std::string_view old_key,
                                        std::string new_key) {
    auto found = object.values.find(std::string(old_key));
    CHECK(found != object.values.end());
    auto value = std::move(found->second);
    object.values.erase(found);
    CHECK(object.values.emplace(new_key, std::move(value)).second);
    const auto order = std::find_if(
        object.key_order.begin(), object.key_order.end(),
        [old_key](const patchy::psd::DescriptorObject::KeyEntry& entry) {
          return entry.key == old_key;
        });
    CHECK(order != object.key_order.end());
    order->key = std::move(new_key);
    // Photoshop's short aliases are stringIDs, not padded four-byte charIDs.
    order->long_form = true;
  };
  const std::vector<std::string> root_filter_shape{
      "enab-", "validAtPosition+", "filterMaskEnable+", "filterMaskLinked+",
      "filterMaskExtendWithWhite+", "filterFXList+"};
  const std::vector<std::string> entry_shape{
      "Nm  -", "blendOptions+", "enab-", "hasoptions+", "FrgC-", "BckC-", "Fltr-",
      "filterID+"};

  const auto authored =
      patchy::psd::author_placed_layer_sold_payload(placement, placed_uuid, &stack);
  const auto authored_info = patchy::psd::parse_placed_layer_block("SoLd", authored);
  CHECK(authored_info.has_value() && authored_info->smart_filters.has_value());
  const auto& authored_stack = *authored_info->smart_filters;
  CHECK(authored_stack.support == patchy::SmartFilterStackSupport::Supported);
  CHECK(!authored_stack.valid_at_position && !authored_stack.mask.linked);
  CHECK(!authored_stack.mask.extend_with_white);
  CHECK(authored_stack.entries.size() == 1U);
  CHECK(std::abs(require_gaussian_filter(authored_stack.entries.front()).radius_pixels - 2.5) < 1e-9);
  CHECK(std::abs(authored_stack.entries.front().opacity - 0.37) < 1e-9);
  CHECK(authored_stack.entries.front().blend_mode == patchy::BlendMode::Multiply);
  CHECK(authored_stack.entries.front().foreground.red == 1);
  CHECK(authored_stack.entries.front().background.blue == 248);

  auto linked = stack;
  linked.mask.linked = true;
  const auto linked_payload =
      patchy::psd::author_placed_layer_sold_payload(placement, placed_uuid, &linked);
  const auto linked_info =
      patchy::psd::parse_placed_layer_block("SoLd", linked_payload);
  CHECK(linked_info.has_value() && linked_info->smart_filters.has_value());
  CHECK(linked_info->smart_filters->mask.linked);
  CHECK(linked_info->smart_filters->support ==
        patchy::SmartFilterStackSupport::Unsupported);
  bool linked_render_threw = false;
  try {
    (void)patchy::render_smart_filter_stack(
        solid_rgba(1, 1, 255, 255, 255, 255), patchy::Rect{0, 0, 1, 1},
        *linked_info->smart_filters);
  } catch (const std::invalid_argument&) {
    linked_render_threw = true;
  }
  CHECK(linked_render_threw);

  const auto authored_descriptor = descriptor_from_sold(authored);
  const auto root_filter_it = std::find_if(
      authored_descriptor.key_order.begin(), authored_descriptor.key_order.end(),
      [](const patchy::psd::DescriptorObject::KeyEntry& entry) {
        return entry.key == "filterFX";
      });
  CHECK(root_filter_it != authored_descriptor.key_order.end() && root_filter_it->long_form);
  const auto comp_it = std::find_if(
      authored_descriptor.key_order.begin(), authored_descriptor.key_order.end(),
      [](const patchy::psd::DescriptorObject::KeyEntry& entry) {
        return entry.key == "comp";
      });
  CHECK(comp_it != authored_descriptor.key_order.end() && root_filter_it < comp_it);
  const auto* filter_root = patchy::psd::descriptor_object(authored_descriptor, "filterFX");
  CHECK(filter_root != nullptr && filter_root->class_id == "filterFXStyle");
  CHECK(filter_root->class_id_long_form);
  CHECK(key_shape(*filter_root) == root_filter_shape);
  const auto* filter_list = patchy::psd::descriptor_value(*filter_root, "filterFXList");
  CHECK(filter_list != nullptr && filter_list->type == patchy::psd::DescriptorValue::Type::List);
  CHECK(filter_list->list_value.size() == 1U);
  CHECK(filter_list->list_value.front().object_value != nullptr);
  const auto& filter_entry = *filter_list->list_value.front().object_value;
  CHECK(filter_entry.class_id == "filterFX" && filter_entry.class_id_long_form);
  CHECK(key_shape(filter_entry) == entry_shape);
  const auto* native_filter = patchy::psd::descriptor_object(filter_entry, "Fltr");
  CHECK(native_filter != nullptr && native_filter->class_id == "GsnB");
  CHECK((key_shape(*native_filter) == std::vector<std::string>{"Rds -"}));
  const auto* radius = patchy::psd::descriptor_value(*native_filter, "Rds ");
  CHECK(radius != nullptr && radius->type == patchy::psd::DescriptorValue::Type::UnitFloat);
  CHECK(radius->unit == "#Pxl" && std::abs(radius->double_value - 2.5) < 1e-9);

  const auto base = patchy::psd::author_placed_layer_sold_payload(placement, placed_uuid);
  const patchy::psd::SmartFilterDescriptorEdit add_edit{
      patchy::psd::SmartFilterDescriptorAction::Replace, &stack};
  const auto added = patchy::psd::regenerate_placed_layer_payload(
      "SoLd", base, placement, nullptr, placed_uuid, add_edit);
  CHECK(added.has_value());
  const auto added_descriptor = descriptor_from_sold(*added);
  const auto* added_filter_root =
      patchy::psd::descriptor_object(added_descriptor, "filterFX");
  CHECK(added_filter_root != nullptr);
  CHECK(key_shape(*added_filter_root) == root_filter_shape);

  auto changed = stack;
  std::get<patchy::GaussianBlurSmartFilter>(changed.entries.front().parameters).radius_pixels = 4.5;
  changed.entries.front().enabled = false;
  const patchy::psd::SmartFilterDescriptorEdit replace_edit{
      patchy::psd::SmartFilterDescriptorAction::Replace, &changed};
  const auto patched = patchy::psd::regenerate_placed_layer_payload(
      "SoLd", *added, placement, nullptr, placed_uuid, replace_edit);
  CHECK(patched.has_value());
  const auto patched_info = patchy::psd::parse_placed_layer_block("SoLd", *patched);
  CHECK(patched_info.has_value() && patched_info->smart_filters.has_value());
  CHECK(!patched_info->smart_filters->entries.front().enabled);
  CHECK(std::abs(require_gaussian_filter(patched_info->smart_filters->entries.front()).radius_pixels -
                 4.5) < 1e-9);
  const auto patched_descriptor = descriptor_from_sold(*patched);
  const auto* patched_filter_root =
      patchy::psd::descriptor_object(patched_descriptor, "filterFX");
  CHECK(patched_filter_root != nullptr);
  CHECK(key_shape(*patched_filter_root) == root_filter_shape);

  const patchy::psd::SmartFilterDescriptorEdit remove_edit{
      patchy::psd::SmartFilterDescriptorAction::Remove, nullptr};
  const auto removed = patchy::psd::regenerate_placed_layer_payload(
      "SoLd", *patched, placement, nullptr, placed_uuid, remove_edit);
  CHECK(removed.has_value());
  const auto removed_descriptor = descriptor_from_sold(*removed);
  CHECK(patchy::psd::descriptor_value(removed_descriptor, "filterFX") == nullptr);
  CHECK(std::none_of(removed_descriptor.key_order.begin(), removed_descriptor.key_order.end(),
                     [](const patchy::psd::DescriptorObject::KeyEntry& entry) {
                       return entry.key == "filterFX";
                     }));
  const auto removed_info = patchy::psd::parse_placed_layer_block("SoLd", *removed);
  CHECK(removed_info.has_value() && !removed_info->smart_filters.has_value());

  // Some native descriptors use short stringID aliases for these otherwise
  // four-character fields. Parsing and patch-in-place regeneration accept both
  // shapes and retain the imported aliases and their key-order entries.
  auto alias_descriptor = descriptor_from_sold(authored);
  auto* alias_root = const_cast<patchy::psd::DescriptorObject*>(
      patchy::psd::descriptor_object(alias_descriptor, "filterFX"));
  CHECK(alias_root != nullptr);
  auto* alias_list = const_cast<patchy::psd::DescriptorValue*>(
      patchy::psd::descriptor_value(*alias_root, "filterFXList"));
  CHECK(alias_list != nullptr &&
        alias_list->type == patchy::psd::DescriptorValue::Type::List &&
        alias_list->list_value.size() == 1U &&
        alias_list->list_value.front().object_value != nullptr);
  auto& alias_entry = *alias_list->list_value.front().object_value;
  auto* alias_blend = const_cast<patchy::psd::DescriptorObject*>(
      patchy::psd::descriptor_object(alias_entry, "blendOptions"));
  auto* alias_filter = const_cast<patchy::psd::DescriptorObject*>(
      patchy::psd::descriptor_object(alias_entry, "Fltr"));
  CHECK(alias_blend != nullptr && alias_filter != nullptr);
  rename_descriptor_key(alias_entry, "Nm  ", "Nm");
  rename_descriptor_key(*alias_blend, "Md  ", "Md");
  rename_descriptor_key(*alias_filter, "Rds ", "Rds");
  const auto alias_entry_key_shape = key_shape(alias_entry);
  const auto alias_blend_key_shape = key_shape(*alias_blend);
  const auto alias_filter_key_shape = key_shape(*alias_filter);
  const auto alias_payload = sold_from_descriptor(alias_descriptor);
  const auto alias_info =
      patchy::psd::parse_placed_layer_block("SoLd", alias_payload);
  CHECK(alias_info.has_value() && alias_info->smart_filters.has_value());
  CHECK(alias_info->smart_filters->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(std::abs(require_gaussian_filter(alias_info->smart_filters->entries.front())
                     .radius_pixels -
                 2.5) < 1e-9);

  auto alias_changed = stack;
  alias_changed.entries.front().native_name = "Aliased Gaussian";
  alias_changed.entries.front().opacity = 0.55;
  alias_changed.entries.front().blend_mode = patchy::BlendMode::Screen;
  std::get<patchy::GaussianBlurSmartFilter>(
      alias_changed.entries.front().parameters).radius_pixels = 3.25;
  auto alias_placement = placement;
  for (std::size_t index = 0; index < alias_placement.transform.size(); index += 2U) {
    alias_placement.transform[index] += 5.0;
    alias_placement.transform[index + 1U] += 7.0;
  }
  alias_placement.width = 40.0;
  alias_placement.height = 30.0;
  alias_placement.resolution = 96.0;
  const std::string alias_placed_uuid =
      "bbbbbbbb-cccc-dddd-8eee-ffffffffffff";
  const patchy::psd::SmartFilterDescriptorEdit alias_edit{
      patchy::psd::SmartFilterDescriptorAction::Replace, &alias_changed};
  const auto alias_regenerated = patchy::psd::regenerate_placed_layer_payload(
      "SoLd", alias_payload, alias_placement, nullptr, alias_placed_uuid,
      alias_edit);
  CHECK(alias_regenerated.has_value());
  const auto alias_regenerated_info =
      patchy::psd::parse_placed_layer_block("SoLd", *alias_regenerated);
  CHECK(alias_regenerated_info.has_value() &&
        alias_regenerated_info->smart_filters.has_value());
  CHECK(alias_regenerated_info->smart_filters->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(alias_regenerated_info->placement.transform == alias_placement.transform);
  CHECK(alias_regenerated_info->placement.width == alias_placement.width);
  CHECK(alias_regenerated_info->placement.height == alias_placement.height);
  CHECK(alias_regenerated_info->placement.resolution == alias_placement.resolution);
  CHECK(alias_regenerated_info->placed_uuid == alias_placed_uuid);
  const auto& alias_regenerated_entry =
      alias_regenerated_info->smart_filters->entries.front();
  CHECK(alias_regenerated_entry.native_name == "Aliased Gaussian");
  CHECK(std::abs(alias_regenerated_entry.opacity - 0.55) < 1e-9);
  CHECK(alias_regenerated_entry.blend_mode == patchy::BlendMode::Screen);
  CHECK(std::abs(require_gaussian_filter(alias_regenerated_entry).radius_pixels -
                 3.25) < 1e-9);

  const auto alias_regenerated_descriptor =
      descriptor_from_sold(*alias_regenerated);
  const auto* regenerated_alias_root =
      patchy::psd::descriptor_object(alias_regenerated_descriptor, "filterFX");
  CHECK(regenerated_alias_root != nullptr);
  const auto* regenerated_alias_list =
      patchy::psd::descriptor_value(*regenerated_alias_root, "filterFXList");
  CHECK(regenerated_alias_list != nullptr &&
        regenerated_alias_list->type ==
            patchy::psd::DescriptorValue::Type::List &&
        regenerated_alias_list->list_value.size() == 1U &&
        regenerated_alias_list->list_value.front().object_value != nullptr);
  const auto& regenerated_alias_entry =
      *regenerated_alias_list->list_value.front().object_value;
  const auto* regenerated_alias_blend =
      patchy::psd::descriptor_object(regenerated_alias_entry, "blendOptions");
  const auto* regenerated_alias_filter =
      patchy::psd::descriptor_object(regenerated_alias_entry, "Fltr");
  CHECK(regenerated_alias_blend != nullptr && regenerated_alias_filter != nullptr);
  CHECK(key_shape(regenerated_alias_entry) == alias_entry_key_shape);
  CHECK(key_shape(*regenerated_alias_blend) == alias_blend_key_shape);
  CHECK(key_shape(*regenerated_alias_filter) == alias_filter_key_shape);
  CHECK(regenerated_alias_entry.values.contains("Nm"));
  CHECK(!regenerated_alias_entry.values.contains("Nm  "));
  CHECK(regenerated_alias_blend->values.contains("Md"));
  CHECK(!regenerated_alias_blend->values.contains("Md  "));
  CHECK(regenerated_alias_filter->values.contains("Rds"));
  CHECK(!regenerated_alias_filter->values.contains("Rds "));
  const auto alias_key_is_long = [](const patchy::psd::DescriptorObject& object,
                                    std::string_view key) {
    const auto found = std::find_if(
        object.key_order.begin(), object.key_order.end(),
        [key](const patchy::psd::DescriptorObject::KeyEntry& entry) {
          return entry.key == key;
        });
    return found != object.key_order.end() && found->long_form;
  };
  CHECK(alias_key_is_long(regenerated_alias_entry, "Nm"));
  CHECK(alias_key_is_long(*regenerated_alias_blend, "Md"));
  CHECK(alias_key_is_long(*regenerated_alias_filter, "Rds"));

  // Structural edits use the desired entry's source index to retain every
  // unknown field and on-disk id form from a native item. Repeated indices
  // must deep-clone descriptor objects: the independently patched radii below
  // would collapse to the last value if the shared_ptr object graph aliased.
  auto native_pair = stack;
  native_pair.entries.push_back(native_pair.entries.front());
  native_pair.entries[0].native_name = "Native A";
  native_pair.entries[1].native_name = "Native B";
  std::get<patchy::GaussianBlurSmartFilter>(
      native_pair.entries[0].parameters).radius_pixels = 1.25;
  std::get<patchy::GaussianBlurSmartFilter>(
      native_pair.entries[1].parameters).radius_pixels = 6.75;
  auto native_pair_descriptor = descriptor_from_sold(
      patchy::psd::author_placed_layer_sold_payload(
          placement, placed_uuid, &native_pair));
  auto* native_pair_root = const_cast<patchy::psd::DescriptorObject*>(
      patchy::psd::descriptor_object(native_pair_descriptor, "filterFX"));
  CHECK(native_pair_root != nullptr);
  auto* native_pair_list = const_cast<patchy::psd::DescriptorValue*>(
      patchy::psd::descriptor_value(*native_pair_root, "filterFXList"));
  CHECK(native_pair_list != nullptr &&
        native_pair_list->type == patchy::psd::DescriptorValue::Type::List &&
        native_pair_list->list_value.size() == 2U);
  for (std::size_t index = 0; index < 2U; ++index) {
    auto& item = *native_pair_list->list_value[index].object_value;
    patchy::psd::DescriptorValue future_value;
    future_value.type = patchy::psd::DescriptorValue::Type::String;
    future_value.string_value = index == 0U ? "unknown-a" : "unknown-b";
    item.key_order.push_back({"futureNativeField", true});
    item.values.emplace("futureNativeField", std::move(future_value));
  }
  const auto native_pair_payload = sold_from_descriptor(native_pair_descriptor);

  auto desired = native_pair;
  desired.entries = {native_pair.entries[1], native_pair.entries[0],
                     native_pair.entries[1], native_pair.entries[0]};
  const std::array<double, 4> desired_radii{2.0, 3.0, 4.0, 5.0};
  for (std::size_t index = 0; index < desired.entries.size(); ++index) {
    desired.entries[index].native_name = "Desired " + std::to_string(index);
    std::get<patchy::GaussianBlurSmartFilter>(
        desired.entries[index].parameters).radius_pixels =
        desired_radii[index];
  }
  patchy::psd::SmartFilterDescriptorEdit structural_edit{
      patchy::psd::SmartFilterDescriptorAction::Replace, &desired};
  structural_edit.entry_sources = {
      std::size_t{1}, std::size_t{0}, std::size_t{1}, std::nullopt};
  const auto structurally_regenerated =
      patchy::psd::regenerate_placed_layer_payload(
          "SoLd", native_pair_payload, placement, nullptr, placed_uuid,
          structural_edit);
  CHECK(structurally_regenerated.has_value());
  const auto structural_info = patchy::psd::parse_placed_layer_block(
      "SoLd", *structurally_regenerated);
  CHECK(structural_info.has_value() &&
        structural_info->smart_filters.has_value());
  CHECK(structural_info->smart_filters->entries.size() == 4U);
  for (std::size_t index = 0; index < desired_radii.size(); ++index) {
    CHECK(std::abs(require_gaussian_filter(
                       structural_info->smart_filters->entries[index])
                       .radius_pixels -
                   desired_radii[index]) < 1e-9);
  }
  const auto structural_descriptor =
      descriptor_from_sold(*structurally_regenerated);
  const auto* structural_root =
      patchy::psd::descriptor_object(structural_descriptor, "filterFX");
  CHECK(structural_root != nullptr);
  const auto* structural_list =
      patchy::psd::descriptor_value(*structural_root, "filterFXList");
  CHECK(structural_list != nullptr &&
        structural_list->type == patchy::psd::DescriptorValue::Type::List &&
        structural_list->list_value.size() == 4U);
  const std::array<std::optional<std::string_view>, 4> expected_unknowns{
      "unknown-b", "unknown-a", "unknown-b", std::nullopt};
  for (std::size_t index = 0; index < expected_unknowns.size(); ++index) {
    const auto& item = *structural_list->list_value[index].object_value;
    const auto* future =
        patchy::psd::descriptor_value(item, "futureNativeField");
    if (!expected_unknowns[index].has_value()) {
      CHECK(future == nullptr);
      continue;
    }
    CHECK(future != nullptr &&
          future->type == patchy::psd::DescriptorValue::Type::String);
    CHECK(future->string_value == *expected_unknowns[index]);
    CHECK(alias_key_is_long(item, "futureNativeField"));
  }

  // Omitting source indices deletes those native items. The retained item
  // continues to carry its unknown native field and stringID key form.
  auto reduced = desired;
  reduced.entries = {desired.entries[1]};
  std::get<patchy::GaussianBlurSmartFilter>(
      reduced.entries.front().parameters).radius_pixels = 8.5;
  patchy::psd::SmartFilterDescriptorEdit delete_edit{
      patchy::psd::SmartFilterDescriptorAction::Replace, &reduced};
  delete_edit.entry_sources = {std::size_t{1}};
  const auto reduced_payload = patchy::psd::regenerate_placed_layer_payload(
      "SoLd", *structurally_regenerated, placement, nullptr, placed_uuid,
      delete_edit);
  CHECK(reduced_payload.has_value());
  const auto reduced_descriptor = descriptor_from_sold(*reduced_payload);
  const auto* reduced_root =
      patchy::psd::descriptor_object(reduced_descriptor, "filterFX");
  CHECK(reduced_root != nullptr);
  const auto* reduced_list =
      patchy::psd::descriptor_value(*reduced_root, "filterFXList");
  CHECK(reduced_list != nullptr &&
        reduced_list->type == patchy::psd::DescriptorValue::Type::List &&
        reduced_list->list_value.size() == 1U &&
        reduced_list->list_value.front().object_value != nullptr);
  const auto& reduced_item = *reduced_list->list_value.front().object_value;
  const auto* reduced_future =
      patchy::psd::descriptor_value(reduced_item, "futureNativeField");
  CHECK(reduced_future != nullptr &&
        reduced_future->type == patchy::psd::DescriptorValue::Type::String &&
        reduced_future->string_value == "unknown-a");
  CHECK(alias_key_is_long(reduced_item, "futureNativeField"));

  // A structural source map is a strict contract. It cannot refer past the
  // original list and must line up one-for-one with the desired entries.
  auto invalid_edit = structural_edit;
  invalid_edit.entry_sources = {std::size_t{0}};
  CHECK(!patchy::psd::regenerate_placed_layer_payload(
             "SoLd", native_pair_payload, placement, nullptr, placed_uuid,
             invalid_edit)
             .has_value());
  invalid_edit.entry_sources = {
      std::size_t{0}, std::size_t{1}, std::size_t{2}, std::nullopt};
  CHECK(!patchy::psd::regenerate_placed_layer_payload(
             "SoLd", native_pair_payload, placement, nullptr, placed_uuid,
             invalid_edit)
             .has_value());

  // The entry name is part of the Photoshop filterFX shape. A missing name or
  // a non-TEXT value cannot be safely rewritten as an editable Gaussian entry.
  // Parse those shapes fail-closed and preserve their exact native descriptor
  // through both the regeneration primitive and the full PSD writer. The
  // supported short stringID alias above remains deliberately accepted.
  const auto descriptor_with_malformed_name =
      [&](std::span<const std::uint8_t> sold, bool remove_name) {
        auto descriptor = descriptor_from_sold(sold);
        auto* root = const_cast<patchy::psd::DescriptorObject*>(
            patchy::psd::descriptor_object(descriptor, "filterFX"));
        CHECK(root != nullptr);
        auto* list = const_cast<patchy::psd::DescriptorValue*>(
            patchy::psd::descriptor_value(*root, "filterFXList"));
        CHECK(list != nullptr &&
              list->type == patchy::psd::DescriptorValue::Type::List &&
              list->list_value.size() == 1U &&
              list->list_value.front().object_value != nullptr);
        auto& entry = *list->list_value.front().object_value;
        const auto name = entry.values.find("Nm  ");
        CHECK(name != entry.values.end());
        if (remove_name) {
          entry.values.erase(name);
          entry.key_order.erase(
              std::remove_if(
                  entry.key_order.begin(), entry.key_order.end(),
                  [](const patchy::psd::DescriptorObject::KeyEntry& key) {
                    return key.key == "Nm  ";
                  }),
              entry.key_order.end());
        } else {
          patchy::psd::DescriptorValue wrong_type;
          wrong_type.type = patchy::psd::DescriptorValue::Type::Integer;
          wrong_type.integer_value = 7;
          name->second = std::move(wrong_type);
        }
        return descriptor;
      };
  for (const bool remove_name : {true, false}) {
    const auto malformed_payload = sold_from_descriptor(
        descriptor_with_malformed_name(authored, remove_name));
    const auto malformed =
        patchy::psd::parse_placed_layer_block("SoLd", malformed_payload);
    CHECK(malformed.has_value() && malformed->smart_filters.has_value());
    CHECK(malformed->smart_filters->support ==
          patchy::SmartFilterStackSupport::Unsupported);
    CHECK(malformed->smart_filters->entries.size() == 1U);
    CHECK(malformed->smart_filters->entries.front().kind ==
          patchy::SmartFilterKind::Unsupported);
    CHECK(malformed->smart_filters->entries.front().native_name.empty());
    const patchy::psd::SmartFilterDescriptorEdit preserve_edit{
        patchy::psd::SmartFilterDescriptorAction::Preserve,
        &*malformed->smart_filters};
    const auto preserved = patchy::psd::regenerate_placed_layer_payload(
        "SoLd", malformed_payload, placement, nullptr, placed_uuid,
        preserve_edit);
    CHECK(preserved.has_value());
    CHECK(*preserved == malformed_payload);

    auto fixture = patchy::psd::DocumentIo::read_file(
        patchy::test::committed_psd_fixture_path(
            "photoshop-smart-filter-model.psd"));
    const auto target_id =
        require_layer_named(fixture, "Gaussian radius 2.5 fractional").id();
    auto* target = fixture.find_layer(target_id);
    CHECK(target != nullptr);
    auto& blocks = target->unknown_psd_blocks();
    const auto source_block = std::find_if(
        blocks.begin(), blocks.end(),
        [](const patchy::UnknownPsdBlock& block) {
          return block.key == "SoLd" || block.key == "SoLE";
        });
    CHECK(source_block != blocks.end());
    source_block->payload = sold_from_descriptor(
        descriptor_with_malformed_name(source_block->payload, remove_name));
    const auto expected_payload = source_block->payload;
    const auto imported = patchy::psd::parse_placed_layer_block(
        source_block->key, expected_payload);
    CHECK(imported.has_value() && imported->smart_filters.has_value());
    CHECK(imported->smart_filters->support ==
          patchy::SmartFilterStackSupport::Unsupported);
    target->set_smart_filter_stack(*imported->smart_filters);
    patchy::mark_layer_smart_object_block_dirty(*target);

    const auto reread = patchy::psd::DocumentIo::read(
        patchy::psd::DocumentIo::write_layered_rgb8(fixture));
    const auto& reread_target = require_layer_named(
        reread, "Gaussian radius 2.5 fractional");
    const auto& reread_blocks = reread_target.unknown_psd_blocks();
    const auto reread_block = std::find_if(
        reread_blocks.begin(), reread_blocks.end(),
        [](const patchy::UnknownPsdBlock& block) {
          return block.key == "SoLd" || block.key == "SoLE";
        });
    CHECK(reread_block != reread_blocks.end());
    CHECK(reread_block->payload == expected_payload);
    CHECK(require_smart_filter_stack(reread, reread_target.name()).support ==
          patchy::SmartFilterStackSupport::Unsupported);
    CHECK(patchy::smart_object_lock_reason(reread_target) == "filters");
  }

  // Pass Through is meaningful for groups, not a native Smart Filter entry.
  // An imported descriptor using it must keep the whole stack preview-locked
  // rather than reach a renderer that cannot execute the blend semantics.
  auto pass_through_descriptor = descriptor_from_sold(authored);
  auto* pass_through_root = const_cast<patchy::psd::DescriptorObject*>(
      patchy::psd::descriptor_object(pass_through_descriptor, "filterFX"));
  CHECK(pass_through_root != nullptr);
  auto* pass_through_list = const_cast<patchy::psd::DescriptorValue*>(
      patchy::psd::descriptor_value(*pass_through_root, "filterFXList"));
  CHECK(pass_through_list != nullptr &&
        pass_through_list->type == patchy::psd::DescriptorValue::Type::List &&
        pass_through_list->list_value.size() == 1U &&
        pass_through_list->list_value.front().object_value != nullptr);
  auto* pass_through_blend = const_cast<patchy::psd::DescriptorObject*>(
      patchy::psd::descriptor_object(
          *pass_through_list->list_value.front().object_value, "blendOptions"));
  CHECK(pass_through_blend != nullptr);
  auto* pass_through_mode = const_cast<patchy::psd::DescriptorValue*>(
      patchy::psd::descriptor_value(*pass_through_blend, "Md  "));
  CHECK(pass_through_mode != nullptr &&
        pass_through_mode->type == patchy::psd::DescriptorValue::Type::Enum);
  pass_through_mode->enum_value = "passThrough";
  pass_through_mode->enum_value_long_form = true;
  const auto pass_through_payload = sold_from_descriptor(pass_through_descriptor);
  const auto pass_through =
      patchy::psd::parse_placed_layer_block("SoLd", pass_through_payload);
  CHECK(pass_through.has_value() && pass_through->smart_filters.has_value());
  CHECK(pass_through->smart_filters->support ==
        patchy::SmartFilterStackSupport::Unsupported);
  CHECK(pass_through->lock_reason == "filters");
}

void smart_filter_canonical_high_pass_descriptor_round_trips_and_preserves_unknowns() {
  patchy::SmartObjectPlacement placement;
  placement.uuid = "10101010-2020-3030-8444-505050505050";
  placement.transform = {7.0, 9.0, 39.0, 9.0, 39.0, 33.0, 7.0, 33.0};
  placement.width = 32.0;
  placement.height = 24.0;
  placement.resolution = 72.0;
  const std::string placed_uuid = "abababab-cdcd-efef-8111-232323232323";

  auto stack = test_gaussian_smart_filter_stack(2.5);
  auto high_pass = test_high_pass_smart_filter_stack(4.25).entries.front();
  high_pass.opacity = 0.61;
  high_pass.blend_mode = patchy::BlendMode::Overlay;
  high_pass.foreground = patchy::RgbColor{9, 8, 7};
  high_pass.background = patchy::RgbColor{246, 247, 248};
  stack.entries.push_back(high_pass);

  const auto descriptor_from_sold = [](std::span<const std::uint8_t> sold) {
    patchy::psd::BigEndianReader reader(sold);
    CHECK(patchy::psd::key_string(patchy::psd::read_signature(reader)) ==
          "soLD");
    CHECK(reader.read_u32() == 4U);
    CHECK(reader.read_u32() == 16U);
    return patchy::psd::read_descriptor(reader);
  };
  const auto sold_from_descriptor =
      [](const patchy::psd::DescriptorObject& descriptor) {
        patchy::psd::BigEndianWriter writer;
        for (const char ch : {'s', 'o', 'L', 'D'}) {
          writer.write_u8(static_cast<std::uint8_t>(ch));
        }
        writer.write_u32(4U);
        writer.write_u32(16U);
        patchy::psd::write_descriptor(writer, descriptor);
        while ((writer.bytes().size() % 4U) != 0U) {
          writer.write_u8(0U);
        }
        return writer.bytes();
      };

  const auto authored = patchy::psd::author_placed_layer_sold_payload(
      placement, placed_uuid, &stack);
  const auto authored_info =
      patchy::psd::parse_placed_layer_block("SoLd", authored);
  CHECK(authored_info.has_value() && authored_info->smart_filters.has_value());
  CHECK(authored_info->smart_filters->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(authored_info->smart_filters->entries.size() == 2U);
  const auto& parsed_high_pass = authored_info->smart_filters->entries[1];
  CHECK(parsed_high_pass.native_name == "High Pass...");
  CHECK(parsed_high_pass.native_class_id == "HghP");
  CHECK(parsed_high_pass.native_filter_id == 0x48676850U);
  CHECK(std::abs(require_high_pass_filter(parsed_high_pass).radius_pixels -
                 4.25) < 1e-9);
  CHECK(std::abs(parsed_high_pass.opacity - 0.61) < 1e-9);
  CHECK(parsed_high_pass.blend_mode == patchy::BlendMode::Overlay);

  auto descriptor = descriptor_from_sold(authored);
  auto* root = const_cast<patchy::psd::DescriptorObject*>(
      patchy::psd::descriptor_object(descriptor, "filterFX"));
  CHECK(root != nullptr);
  auto* list = const_cast<patchy::psd::DescriptorValue*>(
      patchy::psd::descriptor_value(*root, "filterFXList"));
  CHECK(list != nullptr &&
        list->type == patchy::psd::DescriptorValue::Type::List &&
        list->list_value.size() == 2U &&
        list->list_value[1].object_value != nullptr);
  auto& native_entry = *list->list_value[1].object_value;
  auto* native_filter = const_cast<patchy::psd::DescriptorObject*>(
      patchy::psd::descriptor_object(native_entry, "Fltr"));
  CHECK(native_filter != nullptr && native_filter->class_id == "HghP" &&
        !native_filter->class_id_long_form);
  const auto* native_radius =
      patchy::psd::descriptor_value(*native_filter, "Rds ");
  CHECK(native_radius != nullptr &&
        native_radius->type == patchy::psd::DescriptorValue::Type::UnitFloat &&
        native_radius->unit == "#Pxl" &&
        std::abs(native_radius->double_value - 4.25) < 1e-9);
  patchy::psd::DescriptorValue future;
  future.type = patchy::psd::DescriptorValue::Type::String;
  future.string_value = "keep-high-pass-native-data";
  native_entry.key_order.push_back({"futureHighPassField", true});
  native_entry.values.emplace("futureHighPassField", std::move(future));
  const auto payload_with_unknown = sold_from_descriptor(descriptor);

  auto edited = stack;
  auto& edited_high_pass = edited.entries[1];
  std::get<patchy::HighPassSmartFilter>(edited_high_pass.parameters)
      .radius_pixels = 1000.0;
  edited_high_pass.opacity = 0.37;
  edited_high_pass.blend_mode = patchy::BlendMode::Multiply;
  const patchy::psd::SmartFilterDescriptorEdit edit{
      patchy::psd::SmartFilterDescriptorAction::Replace, &edited};
  const auto regenerated = patchy::psd::regenerate_placed_layer_payload(
      "SoLd", payload_with_unknown, placement, nullptr, placed_uuid, edit);
  CHECK(regenerated.has_value());
  const auto regenerated_info =
      patchy::psd::parse_placed_layer_block("SoLd", *regenerated);
  CHECK(regenerated_info.has_value() &&
        regenerated_info->smart_filters.has_value());
  CHECK(regenerated_info->smart_filters->support ==
        patchy::SmartFilterStackSupport::Supported);
  const auto& reread_high_pass =
      regenerated_info->smart_filters->entries[1];
  CHECK(std::abs(require_high_pass_filter(reread_high_pass).radius_pixels -
                 1000.0) < 1e-9);
  CHECK(std::abs(reread_high_pass.opacity - 0.37) < 1e-9);
  CHECK(reread_high_pass.blend_mode == patchy::BlendMode::Multiply);

  const auto regenerated_descriptor = descriptor_from_sold(*regenerated);
  const auto* regenerated_root =
      patchy::psd::descriptor_object(regenerated_descriptor, "filterFX");
  CHECK(regenerated_root != nullptr);
  const auto* regenerated_list =
      patchy::psd::descriptor_value(*regenerated_root, "filterFXList");
  CHECK(regenerated_list != nullptr &&
        regenerated_list->list_value.size() == 2U &&
        regenerated_list->list_value[1].object_value != nullptr);
  const auto* preserved = patchy::psd::descriptor_value(
      *regenerated_list->list_value[1].object_value, "futureHighPassField");
  CHECK(preserved != nullptr &&
        preserved->type == patchy::psd::DescriptorValue::Type::String &&
        preserved->string_value == "keep-high-pass-native-data");
}

void smart_filter_canonical_median_descriptor_round_trips_and_preserves_unknowns() {
  patchy::SmartObjectPlacement placement;
  placement.uuid = "20202020-3030-4040-8555-606060606060";
  placement.transform = {5.0, 7.0, 37.0, 7.0, 37.0, 31.0, 5.0, 31.0};
  placement.width = 32.0;
  placement.height = 24.0;
  placement.resolution = 72.0;
  const std::string placed_uuid = "bcbcbcbc-dede-fafa-8222-343434343434";

  auto stack = test_median_smart_filter_stack(7.5);
  stack.entries.front().opacity = 0.61;
  stack.entries.front().blend_mode = patchy::BlendMode::Overlay;

  const auto descriptor_from_sold = [](std::span<const std::uint8_t> sold) {
    patchy::psd::BigEndianReader reader(sold);
    CHECK(patchy::psd::key_string(patchy::psd::read_signature(reader)) ==
          "soLD");
    CHECK(reader.read_u32() == 4U);
    CHECK(reader.read_u32() == 16U);
    return patchy::psd::read_descriptor(reader);
  };
  const auto sold_from_descriptor =
      [](const patchy::psd::DescriptorObject& descriptor) {
        patchy::psd::BigEndianWriter writer;
        for (const char ch : {'s', 'o', 'L', 'D'}) {
          writer.write_u8(static_cast<std::uint8_t>(ch));
        }
        writer.write_u32(4U);
        writer.write_u32(16U);
        patchy::psd::write_descriptor(writer, descriptor);
        while ((writer.bytes().size() % 4U) != 0U) {
          writer.write_u8(0U);
        }
        return writer.bytes();
      };

  const auto authored = patchy::psd::author_placed_layer_sold_payload(
      placement, placed_uuid, &stack);
  const auto authored_info =
      patchy::psd::parse_placed_layer_block("SoLd", authored);
  CHECK(authored_info.has_value() && authored_info->smart_filters.has_value());
  CHECK(authored_info->smart_filters->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(authored_info->smart_filters->entries.size() == 1U);
  const auto& parsed = authored_info->smart_filters->entries.front();
  CHECK(parsed.native_name == "Median...");
  CHECK(parsed.native_class_id == "Mdn ");
  CHECK(parsed.native_filter_id == 0x4d646e20U);
  CHECK(std::abs(require_median_filter(parsed).radius_pixels - 7.5) < 1e-9);
  CHECK(std::abs(parsed.opacity - 0.61) < 1e-9);
  CHECK(parsed.blend_mode == patchy::BlendMode::Overlay);

  auto descriptor = descriptor_from_sold(authored);
  auto* root = const_cast<patchy::psd::DescriptorObject*>(
      patchy::psd::descriptor_object(descriptor, "filterFX"));
  CHECK(root != nullptr);
  auto* list = const_cast<patchy::psd::DescriptorValue*>(
      patchy::psd::descriptor_value(*root, "filterFXList"));
  CHECK(list != nullptr &&
        list->type == patchy::psd::DescriptorValue::Type::List &&
        list->list_value.size() == 1U &&
        list->list_value.front().object_value != nullptr);
  auto& native_entry = *list->list_value.front().object_value;
  auto* native_filter = const_cast<patchy::psd::DescriptorObject*>(
      patchy::psd::descriptor_object(native_entry, "Fltr"));
  CHECK(native_filter != nullptr && native_filter->class_id == "Mdn " &&
        !native_filter->class_id_long_form);
  const auto* native_radius =
      patchy::psd::descriptor_value(*native_filter, "Rds ");
  CHECK(native_radius != nullptr &&
        native_radius->type == patchy::psd::DescriptorValue::Type::UnitFloat &&
        native_radius->unit == "#Pxl" &&
        std::abs(native_radius->double_value - 7.5) < 1e-9);

  patchy::psd::DescriptorValue future;
  future.type = patchy::psd::DescriptorValue::Type::String;
  future.string_value = "keep-median-native-data";
  native_entry.key_order.push_back({"futureMedianField", true});
  native_entry.values.emplace("futureMedianField", std::move(future));
  const auto payload_with_unknown = sold_from_descriptor(descriptor);

  auto edited = stack;
  std::get<patchy::MedianSmartFilter>(edited.entries.front().parameters)
      .radius_pixels = 500.0;
  edited.entries.front().opacity = 0.37;
  edited.entries.front().blend_mode = patchy::BlendMode::Multiply;
  const patchy::psd::SmartFilterDescriptorEdit edit{
      patchy::psd::SmartFilterDescriptorAction::Replace, &edited};
  const auto regenerated = patchy::psd::regenerate_placed_layer_payload(
      "SoLd", payload_with_unknown, placement, nullptr, placed_uuid, edit);
  CHECK(regenerated.has_value());
  const auto regenerated_info =
      patchy::psd::parse_placed_layer_block("SoLd", *regenerated);
  CHECK(regenerated_info.has_value() &&
        regenerated_info->smart_filters.has_value());
  CHECK(regenerated_info->smart_filters->support ==
        patchy::SmartFilterStackSupport::Supported);
  const auto& reread = regenerated_info->smart_filters->entries.front();
  CHECK(std::abs(require_median_filter(reread).radius_pixels - 500.0) <
        1e-9);
  CHECK(std::abs(reread.opacity - 0.37) < 1e-9);
  CHECK(reread.blend_mode == patchy::BlendMode::Multiply);

  const auto regenerated_descriptor = descriptor_from_sold(*regenerated);
  const auto* regenerated_root =
      patchy::psd::descriptor_object(regenerated_descriptor, "filterFX");
  CHECK(regenerated_root != nullptr);
  const auto* regenerated_list =
      patchy::psd::descriptor_value(*regenerated_root, "filterFXList");
  CHECK(regenerated_list != nullptr &&
        regenerated_list->list_value.size() == 1U &&
        regenerated_list->list_value.front().object_value != nullptr);
  const auto* preserved = patchy::psd::descriptor_value(
      *regenerated_list->list_value.front().object_value,
      "futureMedianField");
  CHECK(preserved != nullptr &&
        preserved->type == patchy::psd::DescriptorValue::Type::String &&
        preserved->string_value == "keep-median-native-data");
}

void smart_filter_canonical_dust_and_scratches_descriptor_round_trips_and_preserves_unknowns() {
  patchy::SmartObjectPlacement placement;
  placement.uuid = "30303030-4040-5050-8666-707070707070";
  placement.transform = {5.0, 7.0, 37.0, 7.0, 37.0, 31.0, 5.0, 31.0};
  placement.width = 32.0;
  placement.height = 24.0;
  placement.resolution = 72.0;
  const std::string placed_uuid = "cdcdcdcd-efef-abab-8333-454545454545";

  auto stack = test_dust_and_scratches_smart_filter_stack(7, 23);
  stack.entries.front().opacity = 0.61;
  stack.entries.front().blend_mode = patchy::BlendMode::Overlay;

  const auto descriptor_from_sold = [](std::span<const std::uint8_t> sold) {
    patchy::psd::BigEndianReader reader(sold);
    CHECK(patchy::psd::key_string(patchy::psd::read_signature(reader)) ==
          "soLD");
    CHECK(reader.read_u32() == 4U);
    CHECK(reader.read_u32() == 16U);
    return patchy::psd::read_descriptor(reader);
  };
  const auto sold_from_descriptor =
      [](const patchy::psd::DescriptorObject& descriptor) {
        patchy::psd::BigEndianWriter writer;
        for (const char ch : {'s', 'o', 'L', 'D'}) {
          writer.write_u8(static_cast<std::uint8_t>(ch));
        }
        writer.write_u32(4U);
        writer.write_u32(16U);
        patchy::psd::write_descriptor(writer, descriptor);
        while ((writer.bytes().size() % 4U) != 0U) {
          writer.write_u8(0U);
        }
        return writer.bytes();
      };

  const auto authored = patchy::psd::author_placed_layer_sold_payload(
      placement, placed_uuid, &stack);
  const auto authored_info =
      patchy::psd::parse_placed_layer_block("SoLd", authored);
  CHECK(authored_info.has_value() && authored_info->smart_filters.has_value());
  CHECK(authored_info->smart_filters->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(authored_info->smart_filters->entries.size() == 1U);
  const auto& parsed = authored_info->smart_filters->entries.front();
  CHECK(parsed.native_name == "Dust && Scratches...");
  CHECK(parsed.native_class_id == "DstS");
  CHECK(parsed.native_filter_id == 0x44737453U);
  CHECK(require_dust_and_scratches_filter(parsed).radius_pixels == 7);
  CHECK(require_dust_and_scratches_filter(parsed).threshold == 23);
  CHECK(std::abs(parsed.opacity - 0.61) < 1e-9);
  CHECK(parsed.blend_mode == patchy::BlendMode::Overlay);

  auto descriptor = descriptor_from_sold(authored);
  auto* root = const_cast<patchy::psd::DescriptorObject*>(
      patchy::psd::descriptor_object(descriptor, "filterFX"));
  CHECK(root != nullptr);
  auto* list = const_cast<patchy::psd::DescriptorValue*>(
      patchy::psd::descriptor_value(*root, "filterFXList"));
  CHECK(list != nullptr &&
        list->type == patchy::psd::DescriptorValue::Type::List &&
        list->list_value.size() == 1U &&
        list->list_value.front().object_value != nullptr);
  auto& native_entry = *list->list_value.front().object_value;
  auto* native_filter = const_cast<patchy::psd::DescriptorObject*>(
      patchy::psd::descriptor_object(native_entry, "Fltr"));
  CHECK(native_filter != nullptr && native_filter->class_id == "DstS" &&
        !native_filter->class_id_long_form);
  CHECK(native_filter->name == "Dust & Scratches");
  CHECK(native_filter->key_order.size() == 2U);
  CHECK(native_filter->key_order[0].key == "Rds " &&
        !native_filter->key_order[0].long_form);
  CHECK(native_filter->key_order[1].key == "Thsh" &&
        !native_filter->key_order[1].long_form);
  const auto* native_radius =
      patchy::psd::descriptor_value(*native_filter, "Rds ");
  const auto* native_threshold =
      patchy::psd::descriptor_value(*native_filter, "Thsh");
  CHECK(native_radius != nullptr &&
        native_radius->type == patchy::psd::DescriptorValue::Type::Integer &&
        native_radius->integer_value == 7);
  CHECK(native_threshold != nullptr &&
        native_threshold->type ==
            patchy::psd::DescriptorValue::Type::Integer &&
        native_threshold->integer_value == 23);

  patchy::psd::DescriptorValue future;
  future.type = patchy::psd::DescriptorValue::Type::String;
  future.string_value = "keep-dust-native-data";
  native_filter->key_order.push_back({"futureDustField", true});
  native_filter->values.emplace("futureDustField", std::move(future));
  // Photoshop descriptors can encode a four-character class through the
  // length-prefixed stringID form. It remains the same semantic class and
  // must stay editable without normalizing its imported on-disk form.
  native_filter->class_id_long_form = true;
  const auto payload_with_unknown = sold_from_descriptor(descriptor);
  const auto alternate_class_info =
      patchy::psd::parse_placed_layer_block("SoLd", payload_with_unknown);
  CHECK(alternate_class_info.has_value() &&
        alternate_class_info->smart_filters.has_value());
  CHECK(alternate_class_info->smart_filters->support ==
        patchy::SmartFilterStackSupport::Supported);

  auto edited = stack;
  auto& edited_dust = std::get<patchy::DustAndScratchesSmartFilter>(
      edited.entries.front().parameters);
  edited_dust.radius_pixels = 100;
  edited_dust.threshold = 255;
  edited.entries.front().opacity = 0.37;
  edited.entries.front().blend_mode = patchy::BlendMode::Multiply;
  const patchy::psd::SmartFilterDescriptorEdit edit{
      patchy::psd::SmartFilterDescriptorAction::Replace, &edited};
  const auto regenerated = patchy::psd::regenerate_placed_layer_payload(
      "SoLd", payload_with_unknown, placement, nullptr, placed_uuid, edit);
  CHECK(regenerated.has_value());
  const auto regenerated_info =
      patchy::psd::parse_placed_layer_block("SoLd", *regenerated);
  CHECK(regenerated_info.has_value() &&
        regenerated_info->smart_filters.has_value());
  CHECK(regenerated_info->smart_filters->support ==
        patchy::SmartFilterStackSupport::Supported);
  const auto& reread = regenerated_info->smart_filters->entries.front();
  CHECK(require_dust_and_scratches_filter(reread).radius_pixels == 100);
  CHECK(require_dust_and_scratches_filter(reread).threshold == 255);
  CHECK(std::abs(reread.opacity - 0.37) < 1e-9);
  CHECK(reread.blend_mode == patchy::BlendMode::Multiply);

  auto regenerated_descriptor = descriptor_from_sold(*regenerated);
  auto* regenerated_root = const_cast<patchy::psd::DescriptorObject*>(
      patchy::psd::descriptor_object(regenerated_descriptor, "filterFX"));
  CHECK(regenerated_root != nullptr);
  auto* regenerated_list = const_cast<patchy::psd::DescriptorValue*>(
      patchy::psd::descriptor_value(*regenerated_root, "filterFXList"));
  CHECK(regenerated_list != nullptr &&
        regenerated_list->list_value.size() == 1U &&
        regenerated_list->list_value.front().object_value != nullptr);
  auto* regenerated_filter = const_cast<patchy::psd::DescriptorObject*>(
      patchy::psd::descriptor_object(
          *regenerated_list->list_value.front().object_value, "Fltr"));
  CHECK(regenerated_filter != nullptr &&
        regenerated_filter->class_id == "DstS" &&
        regenerated_filter->class_id_long_form);
  CHECK(regenerated_filter->key_order.size() == 3U);
  CHECK(regenerated_filter->key_order[0].key == "Rds ");
  CHECK(regenerated_filter->key_order[1].key == "Thsh");
  CHECK(regenerated_filter->key_order[2].key == "futureDustField");
  const auto* preserved = patchy::psd::descriptor_value(
      *regenerated_filter, "futureDustField");
  CHECK(preserved != nullptr &&
        preserved->type == patchy::psd::DescriptorValue::Type::String &&
        preserved->string_value == "keep-dust-native-data");
  CHECK(patchy::psd::descriptor_value(*regenerated_filter, "Rds ")->type ==
        patchy::psd::DescriptorValue::Type::Integer);
  CHECK(patchy::psd::descriptor_value(*regenerated_filter, "Thsh")->type ==
        patchy::psd::DescriptorValue::Type::Integer);

  // A native-class mismatch must fail the complete stack closed while still
  // retaining the uninterpreted entry for byte-preserving resaves.
  regenerated_filter->class_id = "ZZZZ";
  const auto corrupted_payload =
      sold_from_descriptor(regenerated_descriptor);
  const auto corrupted_info =
      patchy::psd::parse_placed_layer_block("SoLd", corrupted_payload);
  CHECK(corrupted_info.has_value() && corrupted_info->smart_filters.has_value());
  CHECK(corrupted_info->smart_filters->support ==
        patchy::SmartFilterStackSupport::Unsupported);
  CHECK(corrupted_info->smart_filters->entries.size() == 1U);
  CHECK(corrupted_info->smart_filters->entries.front().native_class_id ==
        "ZZZZ");
  CHECK(corrupted_info->smart_filters->entries.front().kind ==
        patchy::SmartFilterKind::Unsupported);

  auto mixed = test_gaussian_smart_filter_stack(1.5);
  mixed.entries.push_back(
      test_dust_and_scratches_smart_filter_stack(3, 19).entries.front());
  mixed.entries.push_back(test_median_smart_filter_stack(2.0).entries.front());
  const auto mixed_payload = patchy::psd::author_placed_layer_sold_payload(
      placement, "dededede-f0f0-acac-8444-565656565656", &mixed);
  const auto mixed_info =
      patchy::psd::parse_placed_layer_block("SoLd", mixed_payload);
  CHECK(mixed_info.has_value() && mixed_info->smart_filters.has_value());
  CHECK(mixed_info->smart_filters->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(mixed_info->smart_filters->entries.size() == 3U);
  CHECK(mixed_info->smart_filters->entries[0].kind ==
        patchy::SmartFilterKind::GaussianBlur);
  CHECK(mixed_info->smart_filters->entries[1].kind ==
        patchy::SmartFilterKind::DustAndScratches);
  CHECK(mixed_info->smart_filters->entries[2].kind ==
        patchy::SmartFilterKind::Median);
}

void smart_filter_canonical_surface_blur_descriptor_round_trips_and_preserves_unknowns() {
  patchy::SmartObjectPlacement placement;
  placement.uuid = "40404040-5050-6060-8777-808080808080";
  placement.transform = {5.0, 7.0, 37.0, 7.0, 37.0, 31.0, 5.0, 31.0};
  placement.width = 32.0;
  placement.height = 24.0;
  placement.resolution = 72.0;
  const std::string placed_uuid = "dededede-fafa-bcbc-8444-565656565656";

  auto stack = test_surface_blur_smart_filter_stack(9.25, 31);
  stack.entries.front().opacity = 0.61;
  stack.entries.front().blend_mode = patchy::BlendMode::Overlay;

  const auto descriptor_from_sold = [](std::span<const std::uint8_t> sold) {
    patchy::psd::BigEndianReader reader(sold);
    CHECK(patchy::psd::key_string(patchy::psd::read_signature(reader)) ==
          "soLD");
    CHECK(reader.read_u32() == 4U);
    CHECK(reader.read_u32() == 16U);
    return patchy::psd::read_descriptor(reader);
  };
  const auto sold_from_descriptor =
      [](const patchy::psd::DescriptorObject& descriptor) {
        patchy::psd::BigEndianWriter writer;
        for (const char ch : {'s', 'o', 'L', 'D'}) {
          writer.write_u8(static_cast<std::uint8_t>(ch));
        }
        writer.write_u32(4U);
        writer.write_u32(16U);
        patchy::psd::write_descriptor(writer, descriptor);
        while ((writer.bytes().size() % 4U) != 0U) {
          writer.write_u8(0U);
        }
        return writer.bytes();
      };

  const auto authored = patchy::psd::author_placed_layer_sold_payload(
      placement, placed_uuid, &stack);
  const auto authored_info =
      patchy::psd::parse_placed_layer_block("SoLd", authored);
  CHECK(authored_info.has_value() && authored_info->smart_filters.has_value());
  CHECK(authored_info->smart_filters->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(authored_info->smart_filters->entries.size() == 1U);
  const auto& parsed = authored_info->smart_filters->entries.front();
  CHECK(parsed.native_name == "Surface Blur...");
  CHECK(parsed.native_class_id == "surfaceBlur");
  CHECK(parsed.native_filter_id == 854U);
  CHECK(std::abs(require_surface_blur_filter(parsed).radius_pixels - 9.25) <
        1e-9);
  CHECK(require_surface_blur_filter(parsed).threshold == 31);
  CHECK(std::abs(parsed.opacity - 0.61) < 1e-9);
  CHECK(parsed.blend_mode == patchy::BlendMode::Overlay);

  auto descriptor = descriptor_from_sold(authored);
  auto* root = const_cast<patchy::psd::DescriptorObject*>(
      patchy::psd::descriptor_object(descriptor, "filterFX"));
  CHECK(root != nullptr);
  auto* list = const_cast<patchy::psd::DescriptorValue*>(
      patchy::psd::descriptor_value(*root, "filterFXList"));
  CHECK(list != nullptr &&
        list->type == patchy::psd::DescriptorValue::Type::List &&
        list->list_value.size() == 1U &&
        list->list_value.front().object_value != nullptr);
  auto& native_entry = *list->list_value.front().object_value;
  auto* native_filter = const_cast<patchy::psd::DescriptorObject*>(
      patchy::psd::descriptor_object(native_entry, "Fltr"));
  CHECK(native_filter != nullptr &&
        native_filter->class_id == "surfaceBlur" &&
        native_filter->class_id_long_form);
  CHECK(native_filter->name == "Surface Blur");
  CHECK(native_filter->key_order.size() == 2U);
  CHECK(native_filter->key_order[0].key == "Rds " &&
        !native_filter->key_order[0].long_form);
  CHECK(native_filter->key_order[1].key == "Thsh" &&
        !native_filter->key_order[1].long_form);
  const auto* native_radius =
      patchy::psd::descriptor_value(*native_filter, "Rds ");
  const auto* native_threshold =
      patchy::psd::descriptor_value(*native_filter, "Thsh");
  CHECK(native_radius != nullptr &&
        native_radius->type ==
            patchy::psd::DescriptorValue::Type::UnitFloat &&
        native_radius->unit == "#Pxl" &&
        std::abs(native_radius->double_value - 9.25) < 1e-9);
  CHECK(native_threshold != nullptr &&
        native_threshold->type ==
            patchy::psd::DescriptorValue::Type::Integer &&
        native_threshold->integer_value == 31);

  patchy::psd::DescriptorValue future;
  future.type = patchy::psd::DescriptorValue::Type::String;
  future.string_value = "keep-surface-native-data";
  native_filter->key_order.push_back({"futureSurfaceField", true});
  native_filter->values.emplace("futureSurfaceField", std::move(future));
  const auto payload_with_unknown = sold_from_descriptor(descriptor);

  auto edited = stack;
  auto& edited_surface = std::get<patchy::SurfaceBlurSmartFilter>(
      edited.entries.front().parameters);
  edited_surface.radius_pixels = 100.0;
  edited_surface.threshold = 255;
  edited.entries.front().opacity = 0.37;
  edited.entries.front().blend_mode = patchy::BlendMode::Multiply;
  const patchy::psd::SmartFilterDescriptorEdit edit{
      patchy::psd::SmartFilterDescriptorAction::Replace, &edited};
  const auto regenerated = patchy::psd::regenerate_placed_layer_payload(
      "SoLd", payload_with_unknown, placement, nullptr, placed_uuid, edit);
  CHECK(regenerated.has_value());
  const auto regenerated_info =
      patchy::psd::parse_placed_layer_block("SoLd", *regenerated);
  CHECK(regenerated_info.has_value() &&
        regenerated_info->smart_filters.has_value());
  CHECK(regenerated_info->smart_filters->support ==
        patchy::SmartFilterStackSupport::Supported);
  const auto& reread = regenerated_info->smart_filters->entries.front();
  CHECK(std::abs(require_surface_blur_filter(reread).radius_pixels - 100.0) <
        1e-9);
  CHECK(require_surface_blur_filter(reread).threshold == 255);
  CHECK(std::abs(reread.opacity - 0.37) < 1e-9);
  CHECK(reread.blend_mode == patchy::BlendMode::Multiply);

  auto regenerated_descriptor = descriptor_from_sold(*regenerated);
  auto* regenerated_root = const_cast<patchy::psd::DescriptorObject*>(
      patchy::psd::descriptor_object(regenerated_descriptor, "filterFX"));
  CHECK(regenerated_root != nullptr);
  auto* regenerated_list = const_cast<patchy::psd::DescriptorValue*>(
      patchy::psd::descriptor_value(*regenerated_root, "filterFXList"));
  CHECK(regenerated_list != nullptr &&
        regenerated_list->list_value.size() == 1U &&
        regenerated_list->list_value.front().object_value != nullptr);
  auto* regenerated_filter = const_cast<patchy::psd::DescriptorObject*>(
      patchy::psd::descriptor_object(
          *regenerated_list->list_value.front().object_value, "Fltr"));
  CHECK(regenerated_filter != nullptr &&
        regenerated_filter->class_id == "surfaceBlur" &&
        regenerated_filter->class_id_long_form);
  CHECK(regenerated_filter->key_order.size() == 3U);
  CHECK(regenerated_filter->key_order[0].key == "Rds ");
  CHECK(regenerated_filter->key_order[1].key == "Thsh");
  CHECK(regenerated_filter->key_order[2].key == "futureSurfaceField");
  const auto* preserved = patchy::psd::descriptor_value(
      *regenerated_filter, "futureSurfaceField");
  CHECK(preserved != nullptr &&
        preserved->type == patchy::psd::DescriptorValue::Type::String &&
        preserved->string_value == "keep-surface-native-data");
  CHECK(patchy::psd::descriptor_value(*regenerated_filter, "Rds ")->type ==
        patchy::psd::DescriptorValue::Type::UnitFloat);
  CHECK(patchy::psd::descriptor_value(*regenerated_filter, "Thsh")->type ==
        patchy::psd::DescriptorValue::Type::Integer);

  regenerated_filter->class_id = "ZZZZ";
  regenerated_filter->class_id_long_form = false;
  const auto corrupted_payload = sold_from_descriptor(regenerated_descriptor);
  const auto corrupted_info =
      patchy::psd::parse_placed_layer_block("SoLd", corrupted_payload);
  CHECK(corrupted_info.has_value() && corrupted_info->smart_filters.has_value());
  CHECK(corrupted_info->smart_filters->support ==
        patchy::SmartFilterStackSupport::Unsupported);
  CHECK(corrupted_info->smart_filters->entries.size() == 1U);
  CHECK(corrupted_info->smart_filters->entries.front().native_class_id ==
        "ZZZZ");
  CHECK(corrupted_info->smart_filters->entries.front().kind ==
        patchy::SmartFilterKind::Unsupported);
}

void smart_filter_canonical_plastic_wrap_descriptor_authors_and_patches() {
  patchy::SmartObjectPlacement placement;
  placement.uuid = "41414141-5151-6161-8777-818181818181";
  placement.transform = {3.0, 5.0, 35.0, 5.0, 35.0, 29.0, 3.0, 29.0};
  placement.width = 32.0;
  placement.height = 24.0;
  placement.resolution = 72.0;
  const std::string placed_uuid = "dfdfdfdf-fbfb-bdbd-8444-575757575757";

  patchy::SmartFilterStack stack;
  stack.support = patchy::SmartFilterStackSupport::Supported;
  stack.mask.linked = false;
  patchy::SmartFilterEntry entry;
  entry.kind = patchy::SmartFilterKind::PlasticWrap;
  entry.native_name = "Plastic Wrap...";
  entry.native_class_id = "PlsW";
  entry.native_filter_id = 0x506C7357U;
  entry.parameters = patchy::PlasticWrapSmartFilter{13, 9, 7};
  stack.entries.push_back(std::move(entry));

  const auto descriptor_from_sold = [](std::span<const std::uint8_t> sold) {
    patchy::psd::BigEndianReader reader(sold);
    CHECK(patchy::psd::key_string(patchy::psd::read_signature(reader)) ==
          "soLD");
    CHECK(reader.read_u32() == 4U);
    CHECK(reader.read_u32() == 16U);
    return patchy::psd::read_descriptor(reader);
  };

  const auto authored = patchy::psd::author_placed_layer_sold_payload(
      placement, placed_uuid, &stack);
  const auto authored_info =
      patchy::psd::parse_placed_layer_block("SoLd", authored);
  CHECK(authored_info.has_value() && authored_info->smart_filters.has_value());
  CHECK(authored_info->smart_filters->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(authored_info->smart_filters->entries.size() == 1U);
  const auto &parsed = authored_info->smart_filters->entries.front();
  CHECK(parsed.native_name == "Plastic Wrap...");
  CHECK(parsed.native_class_id == "PlsW");
  CHECK(parsed.native_filter_id == 0x506C7357U);
  CHECK(require_plastic_wrap_filter(parsed).highlight_strength == 13);
  CHECK(require_plastic_wrap_filter(parsed).detail == 9);
  CHECK(require_plastic_wrap_filter(parsed).smoothness == 7);

  auto descriptor = descriptor_from_sold(authored);
  const auto *root = patchy::psd::descriptor_object(descriptor, "filterFX");
  CHECK(root != nullptr);
  const auto *list = patchy::psd::descriptor_value(*root, "filterFXList");
  CHECK(list != nullptr &&
        list->type == patchy::psd::DescriptorValue::Type::List &&
        list->list_value.size() == 1U &&
        list->list_value.front().object_value != nullptr);
  const auto *native_filter = patchy::psd::descriptor_object(
      *list->list_value.front().object_value, "Fltr");
  CHECK(native_filter != nullptr && native_filter->class_id == "PlsW" &&
        !native_filter->class_id_long_form);
  CHECK(native_filter->name == "Plastic Wrap");
  CHECK(native_filter->key_order.size() == 3U);
  CHECK(native_filter->key_order[0].key == "Hghl" &&
        !native_filter->key_order[0].long_form);
  CHECK(native_filter->key_order[1].key == "Dtl " &&
        !native_filter->key_order[1].long_form);
  CHECK(native_filter->key_order[2].key == "Smth" &&
        !native_filter->key_order[2].long_form);
  const auto *highlight =
      patchy::psd::descriptor_value(*native_filter, "Hghl");
  const auto *detail = patchy::psd::descriptor_value(*native_filter, "Dtl ");
  const auto *smoothness =
      patchy::psd::descriptor_value(*native_filter, "Smth");
  CHECK(highlight != nullptr && detail != nullptr && smoothness != nullptr);
  CHECK(highlight->type == patchy::psd::DescriptorValue::Type::Integer &&
        highlight->integer_value == 13);
  CHECK(detail->type == patchy::psd::DescriptorValue::Type::Integer &&
        detail->integer_value == 9);
  CHECK(smoothness->type == patchy::psd::DescriptorValue::Type::Integer &&
        smoothness->integer_value == 7);

  auto edited = stack;
  auto &edited_plastic = std::get<patchy::PlasticWrapSmartFilter>(
      edited.entries.front().parameters);
  edited_plastic.highlight_strength = 20;
  edited_plastic.detail = 15;
  edited_plastic.smoothness = 1;
  const patchy::psd::SmartFilterDescriptorEdit edit{
      patchy::psd::SmartFilterDescriptorAction::Replace, &edited};
  const auto regenerated = patchy::psd::regenerate_placed_layer_payload(
      "SoLd", authored, placement, nullptr, placed_uuid, edit);
  CHECK(regenerated.has_value());
  const auto regenerated_info =
      patchy::psd::parse_placed_layer_block("SoLd", *regenerated);
  CHECK(regenerated_info.has_value() &&
        regenerated_info->smart_filters.has_value());
  const auto &reread =
      regenerated_info->smart_filters->entries.front();
  CHECK(require_plastic_wrap_filter(reread).highlight_strength == 20);
  CHECK(require_plastic_wrap_filter(reread).detail == 15);
  CHECK(require_plastic_wrap_filter(reread).smoothness == 1);
}

void smart_filter_canonical_mosaic_descriptor_authors_and_patches() {
  patchy::SmartObjectPlacement placement;
  placement.uuid = "41414141-5151-6161-8777-828282828282";
  placement.transform = {3.0, 5.0, 35.0, 5.0, 35.0, 29.0, 3.0, 29.0};
  placement.width = 32.0;
  placement.height = 24.0;
  placement.resolution = 72.0;
  const std::string placed_uuid = "dfdfdfdf-fbfb-bdbd-8444-585858585858";

  patchy::SmartFilterStack stack;
  stack.support = patchy::SmartFilterStackSupport::Supported;
  stack.mask.linked = false;
  patchy::SmartFilterEntry entry;
  entry.kind = patchy::SmartFilterKind::Mosaic;
  entry.native_name = "Mosaic...";
  entry.native_class_id = "Msc ";
  entry.native_filter_id = 0x4d736320U;
  entry.parameters = patchy::MosaicSmartFilter{6};
  stack.entries.push_back(std::move(entry));

  const auto descriptor_from_sold = [](std::span<const std::uint8_t> sold) {
    patchy::psd::BigEndianReader reader(sold);
    CHECK(patchy::psd::key_string(patchy::psd::read_signature(reader)) ==
          "soLD");
    CHECK(reader.read_u32() == 4U);
    CHECK(reader.read_u32() == 16U);
    return patchy::psd::read_descriptor(reader);
  };

  const auto authored = patchy::psd::author_placed_layer_sold_payload(
      placement, placed_uuid, &stack);
  const auto authored_info =
      patchy::psd::parse_placed_layer_block("SoLd", authored);
  CHECK(authored_info.has_value() && authored_info->smart_filters.has_value());
  CHECK(authored_info->smart_filters->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(authored_info->smart_filters->entries.size() == 1U);
  const auto &parsed = authored_info->smart_filters->entries.front();
  CHECK(parsed.native_name == "Mosaic...");
  CHECK(parsed.native_class_id == "Msc ");
  CHECK(parsed.native_filter_id == 0x4d736320U);
  CHECK(require_mosaic_filter(parsed).cell_size_pixels == 6);

  auto descriptor = descriptor_from_sold(authored);
  const auto *root = patchy::psd::descriptor_object(descriptor, "filterFX");
  CHECK(root != nullptr);
  const auto *list = patchy::psd::descriptor_value(*root, "filterFXList");
  CHECK(list != nullptr &&
        list->type == patchy::psd::DescriptorValue::Type::List &&
        list->list_value.size() == 1U &&
        list->list_value.front().object_value != nullptr);
  const auto *native_filter = patchy::psd::descriptor_object(
      *list->list_value.front().object_value, "Fltr");
  CHECK(native_filter != nullptr && native_filter->class_id == "Msc " &&
        !native_filter->class_id_long_form);
  CHECK(native_filter->name == "Mosaic");
  CHECK(native_filter->key_order.size() == 1U);
  CHECK(native_filter->key_order[0].key == "ClSz" &&
        !native_filter->key_order[0].long_form);
  const auto *cell_size =
      patchy::psd::descriptor_value(*native_filter, "ClSz");
  CHECK(cell_size != nullptr);
  // Photoshop stores Cell Size as a #Pxl unit double (July 2026 capture).
  CHECK(cell_size->type == patchy::psd::DescriptorValue::Type::UnitFloat &&
        cell_size->unit == "#Pxl" &&
        std::abs(cell_size->double_value - 6.0) < 1e-9);

  auto edited = stack;
  auto &edited_mosaic = std::get<patchy::MosaicSmartFilter>(
      edited.entries.front().parameters);
  edited_mosaic.cell_size_pixels = 24;
  const patchy::psd::SmartFilterDescriptorEdit edit{
      patchy::psd::SmartFilterDescriptorAction::Replace, &edited};
  const auto regenerated = patchy::psd::regenerate_placed_layer_payload(
      "SoLd", authored, placement, nullptr, placed_uuid, edit);
  CHECK(regenerated.has_value());
  const auto regenerated_info =
      patchy::psd::parse_placed_layer_block("SoLd", *regenerated);
  CHECK(regenerated_info.has_value() &&
        regenerated_info->smart_filters.has_value());
  const auto &reread = regenerated_info->smart_filters->entries.front();
  CHECK(require_mosaic_filter(reread).cell_size_pixels == 24);
}

void smart_filter_canonical_emboss_descriptor_authors_and_patches() {
  patchy::SmartObjectPlacement placement;
  placement.uuid = "41414141-5151-6161-8777-838383838383";
  placement.transform = {3.0, 5.0, 35.0, 5.0, 35.0, 29.0, 3.0, 29.0};
  placement.width = 32.0;
  placement.height = 24.0;
  placement.resolution = 72.0;
  const std::string placed_uuid = "dfdfdfdf-fbfb-bdbd-8444-595959595959";

  patchy::SmartFilterStack stack;
  stack.support = patchy::SmartFilterStackSupport::Supported;
  stack.mask.linked = false;
  patchy::SmartFilterEntry entry;
  entry.kind = patchy::SmartFilterKind::Emboss;
  entry.native_name = "Emboss...";
  entry.native_class_id = "Embs";
  entry.native_filter_id = 0x456d6273U;
  entry.parameters = patchy::EmbossSmartFilter{135, 3, 150};
  stack.entries.push_back(std::move(entry));

  const auto descriptor_from_sold = [](std::span<const std::uint8_t> sold) {
    patchy::psd::BigEndianReader reader(sold);
    CHECK(patchy::psd::key_string(patchy::psd::read_signature(reader)) ==
          "soLD");
    CHECK(reader.read_u32() == 4U);
    CHECK(reader.read_u32() == 16U);
    return patchy::psd::read_descriptor(reader);
  };

  const auto authored = patchy::psd::author_placed_layer_sold_payload(
      placement, placed_uuid, &stack);
  const auto authored_info =
      patchy::psd::parse_placed_layer_block("SoLd", authored);
  CHECK(authored_info.has_value() && authored_info->smart_filters.has_value());
  CHECK(authored_info->smart_filters->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(authored_info->smart_filters->entries.size() == 1U);
  const auto &parsed = authored_info->smart_filters->entries.front();
  CHECK(parsed.native_name == "Emboss...");
  CHECK(parsed.native_class_id == "Embs");
  CHECK(parsed.native_filter_id == 0x456d6273U);
  CHECK(require_emboss_filter(parsed).angle_degrees == 135);
  CHECK(require_emboss_filter(parsed).height_pixels == 3);
  CHECK(require_emboss_filter(parsed).amount_percent == 150);

  auto descriptor = descriptor_from_sold(authored);
  const auto *root = patchy::psd::descriptor_object(descriptor, "filterFX");
  CHECK(root != nullptr);
  const auto *list = patchy::psd::descriptor_value(*root, "filterFXList");
  CHECK(list != nullptr &&
        list->type == patchy::psd::DescriptorValue::Type::List &&
        list->list_value.size() == 1U &&
        list->list_value.front().object_value != nullptr);
  const auto *native_filter = patchy::psd::descriptor_object(
      *list->list_value.front().object_value, "Fltr");
  CHECK(native_filter != nullptr && native_filter->class_id == "Embs" &&
        !native_filter->class_id_long_form);
  CHECK(native_filter->name == "Emboss");
  // Photoshop's Emboss key order is Angl, Hght, Amnt, all plain integers
  // (July 2026 capture).
  CHECK(native_filter->key_order.size() == 3U);
  CHECK(native_filter->key_order[0].key == "Angl" &&
        !native_filter->key_order[0].long_form);
  CHECK(native_filter->key_order[1].key == "Hght" &&
        !native_filter->key_order[1].long_form);
  CHECK(native_filter->key_order[2].key == "Amnt" &&
        !native_filter->key_order[2].long_form);
  const auto *angle = patchy::psd::descriptor_value(*native_filter, "Angl");
  const auto *height = patchy::psd::descriptor_value(*native_filter, "Hght");
  const auto *amount = patchy::psd::descriptor_value(*native_filter, "Amnt");
  CHECK(angle != nullptr && height != nullptr && amount != nullptr);
  CHECK(angle->type == patchy::psd::DescriptorValue::Type::Integer &&
        angle->integer_value == 135);
  CHECK(height->type == patchy::psd::DescriptorValue::Type::Integer &&
        height->integer_value == 3);
  CHECK(amount->type == patchy::psd::DescriptorValue::Type::Integer &&
        amount->integer_value == 150);

  auto edited = stack;
  auto &edited_emboss = std::get<patchy::EmbossSmartFilter>(
      edited.entries.front().parameters);
  edited_emboss.angle_degrees = -22;
  edited_emboss.height_pixels = 24;
  edited_emboss.amount_percent = 500;
  const patchy::psd::SmartFilterDescriptorEdit edit{
      patchy::psd::SmartFilterDescriptorAction::Replace, &edited};
  const auto regenerated = patchy::psd::regenerate_placed_layer_payload(
      "SoLd", authored, placement, nullptr, placed_uuid, edit);
  CHECK(regenerated.has_value());
  const auto regenerated_info =
      patchy::psd::parse_placed_layer_block("SoLd", *regenerated);
  CHECK(regenerated_info.has_value() &&
        regenerated_info->smart_filters.has_value());
  const auto &reread = regenerated_info->smart_filters->entries.front();
  CHECK(require_emboss_filter(reread).angle_degrees == -22);
  CHECK(require_emboss_filter(reread).height_pixels == 24);
  CHECK(require_emboss_filter(reread).amount_percent == 500);
}

void smart_filter_canonical_box_blur_descriptor_authors_and_patches() {
  patchy::SmartObjectPlacement placement;
  placement.uuid = "41414141-5151-6161-8777-848484848484";
  placement.transform = {3.0, 5.0, 35.0, 5.0, 35.0, 29.0, 3.0, 29.0};
  placement.width = 32.0;
  placement.height = 24.0;
  placement.resolution = 72.0;
  const std::string placed_uuid = "dfdfdfdf-fbfb-bdbd-8444-606060606060";

  patchy::SmartFilterStack stack;
  stack.support = patchy::SmartFilterStackSupport::Supported;
  stack.mask.linked = false;
  patchy::SmartFilterEntry entry;
  entry.kind = patchy::SmartFilterKind::BoxBlur;
  entry.native_name = "Box Blur...";
  entry.native_class_id = "boxblur";
  entry.native_filter_id = 843U;
  entry.parameters = patchy::BoxBlurSmartFilter{5.0};
  stack.entries.push_back(std::move(entry));

  const auto descriptor_from_sold = [](std::span<const std::uint8_t> sold) {
    patchy::psd::BigEndianReader reader(sold);
    CHECK(patchy::psd::key_string(patchy::psd::read_signature(reader)) ==
          "soLD");
    CHECK(reader.read_u32() == 4U);
    CHECK(reader.read_u32() == 16U);
    return patchy::psd::read_descriptor(reader);
  };

  const auto authored = patchy::psd::author_placed_layer_sold_payload(
      placement, placed_uuid, &stack);
  const auto authored_info =
      patchy::psd::parse_placed_layer_block("SoLd", authored);
  CHECK(authored_info.has_value() && authored_info->smart_filters.has_value());
  CHECK(authored_info->smart_filters->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(authored_info->smart_filters->entries.size() == 1U);
  const auto &parsed = authored_info->smart_filters->entries.front();
  CHECK(parsed.native_name == "Box Blur...");
  CHECK(parsed.native_class_id == "boxblur");
  CHECK(parsed.native_filter_id == 843U);
  CHECK(std::abs(require_box_blur_filter(parsed).radius_pixels - 5.0) < 1e-9);

  auto descriptor = descriptor_from_sold(authored);
  const auto *root = patchy::psd::descriptor_object(descriptor, "filterFX");
  CHECK(root != nullptr);
  const auto *list = patchy::psd::descriptor_value(*root, "filterFXList");
  CHECK(list != nullptr &&
        list->type == patchy::psd::DescriptorValue::Type::List &&
        list->list_value.size() == 1U &&
        list->list_value.front().object_value != nullptr);
  const auto *native_filter = patchy::psd::descriptor_object(
      *list->list_value.front().object_value, "Fltr");
  // Box Blur's class is the full stringID "boxblur" (July 2026 capture).
  CHECK(native_filter != nullptr && native_filter->class_id == "boxblur" &&
        native_filter->class_id_long_form);
  CHECK(native_filter->name == "Box Blur");
  CHECK(native_filter->key_order.size() == 1U);
  CHECK(native_filter->key_order[0].key == "Rds " &&
        !native_filter->key_order[0].long_form);
  const auto *radius = patchy::psd::descriptor_value(*native_filter, "Rds ");
  CHECK(radius != nullptr);
  CHECK(radius->type == patchy::psd::DescriptorValue::Type::UnitFloat &&
        radius->unit == "#Pxl" && std::abs(radius->double_value - 5.0) < 1e-9);

  auto edited = stack;
  auto &edited_box = std::get<patchy::BoxBlurSmartFilter>(
      edited.entries.front().parameters);
  edited_box.radius_pixels = 250.5;
  const patchy::psd::SmartFilterDescriptorEdit edit{
      patchy::psd::SmartFilterDescriptorAction::Replace, &edited};
  const auto regenerated = patchy::psd::regenerate_placed_layer_payload(
      "SoLd", authored, placement, nullptr, placed_uuid, edit);
  CHECK(regenerated.has_value());
  const auto regenerated_info =
      patchy::psd::parse_placed_layer_block("SoLd", *regenerated);
  CHECK(regenerated_info.has_value() &&
        regenerated_info->smart_filters.has_value());
  const auto &reread = regenerated_info->smart_filters->entries.front();
  CHECK(std::abs(require_box_blur_filter(reread).radius_pixels - 250.5) <
        1e-9);
}

}  // namespace

std::vector<patchy::test::TestCase> smart_filter_descriptors_tests_part1() {
  return {
      {"smart_filter_authored_effects_record_round_trips_and_mutates",
       smart_filter_authored_effects_record_round_trips_and_mutates},
      {"smart_filter_effects_author_rejects_oversized_bounds",
       smart_filter_effects_author_rejects_oversized_bounds},
      {"smart_filter_effects_mask_replacement_preserves_native_structure",
       smart_filter_effects_mask_replacement_preserves_native_structure},
      {"smart_filter_effects_mask_replacement_adds_removes_and_fails_closed",
       smart_filter_effects_mask_replacement_adds_removes_and_fails_closed},
      {"smart_filter_canonical_gaussian_descriptor_authors_patches_and_removes",
       smart_filter_canonical_gaussian_descriptor_authors_patches_and_removes},
      {"smart_filter_canonical_high_pass_descriptor_round_trips_and_preserves_unknowns",
       smart_filter_canonical_high_pass_descriptor_round_trips_and_preserves_unknowns},
      {"smart_filter_canonical_median_descriptor_round_trips_and_preserves_unknowns",
       smart_filter_canonical_median_descriptor_round_trips_and_preserves_unknowns},
      {"smart_filter_canonical_dust_and_scratches_descriptor_round_trips_and_preserves_unknowns",
       smart_filter_canonical_dust_and_scratches_descriptor_round_trips_and_preserves_unknowns},
      {"smart_filter_canonical_surface_blur_descriptor_round_trips_and_preserves_unknowns",
       smart_filter_canonical_surface_blur_descriptor_round_trips_and_preserves_unknowns},
      {"smart_filter_canonical_plastic_wrap_descriptor_authors_and_patches",
       smart_filter_canonical_plastic_wrap_descriptor_authors_and_patches},
      {"smart_filter_canonical_mosaic_descriptor_authors_and_patches",
       smart_filter_canonical_mosaic_descriptor_authors_and_patches},
      {"smart_filter_canonical_emboss_descriptor_authors_and_patches",
       smart_filter_canonical_emboss_descriptor_authors_and_patches},
      {"smart_filter_canonical_box_blur_descriptor_authors_and_patches",
       smart_filter_canonical_box_blur_descriptor_authors_and_patches},
  };
}
