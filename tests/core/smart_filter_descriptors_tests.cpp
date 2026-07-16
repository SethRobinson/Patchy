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

using patchy::test::require_layer_named;
using patchy::test::require_smart_filter_stack;
using patchy::test::solid_rgba;
using patchy::test::test_dust_and_scratches_smart_filter_stack;
using patchy::test::test_gaussian_smart_filter_stack;
using patchy::test::test_high_pass_smart_filter_stack;
using patchy::test::test_median_smart_filter_stack;
using patchy::test::test_surface_blur_smart_filter_stack;

const patchy::GaussianBlurSmartFilter& require_gaussian_filter(const patchy::SmartFilterEntry& entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::GaussianBlur);
  const auto* gaussian = std::get_if<patchy::GaussianBlurSmartFilter>(&entry.parameters);
  CHECK(gaussian != nullptr);
  return *gaussian;
}

const patchy::HighPassSmartFilter& require_high_pass_filter(
    const patchy::SmartFilterEntry& entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::HighPass);
  const auto* high_pass =
      std::get_if<patchy::HighPassSmartFilter>(&entry.parameters);
  CHECK(high_pass != nullptr);
  return *high_pass;
}

const patchy::MedianSmartFilter& require_median_filter(
    const patchy::SmartFilterEntry& entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::Median);
  const auto* median =
      std::get_if<patchy::MedianSmartFilter>(&entry.parameters);
  CHECK(median != nullptr);
  return *median;
}

const patchy::DustAndScratchesSmartFilter&
require_dust_and_scratches_filter(const patchy::SmartFilterEntry& entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::DustAndScratches);
  const auto* dust =
      std::get_if<patchy::DustAndScratchesSmartFilter>(&entry.parameters);
  CHECK(dust != nullptr);
  return *dust;
}

const patchy::SurfaceBlurSmartFilter& require_surface_blur_filter(
    const patchy::SmartFilterEntry& entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::SurfaceBlur);
  const auto* surface =
      std::get_if<patchy::SurfaceBlurSmartFilter>(&entry.parameters);
  CHECK(surface != nullptr);
  return *surface;
}

const patchy::PlasticWrapSmartFilter &
require_plastic_wrap_filter(const patchy::SmartFilterEntry &entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::PlasticWrap);
  const auto *plastic =
      std::get_if<patchy::PlasticWrapSmartFilter>(&entry.parameters);
  CHECK(plastic != nullptr);
  return *plastic;
}

const patchy::MosaicSmartFilter &
require_mosaic_filter(const patchy::SmartFilterEntry &entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::Mosaic);
  const auto *mosaic =
      std::get_if<patchy::MosaicSmartFilter>(&entry.parameters);
  CHECK(mosaic != nullptr);
  return *mosaic;
}

const patchy::EmbossSmartFilter &
require_emboss_filter(const patchy::SmartFilterEntry &entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::Emboss);
  const auto *emboss =
      std::get_if<patchy::EmbossSmartFilter>(&entry.parameters);
  CHECK(emboss != nullptr);
  return *emboss;
}

const patchy::BoxBlurSmartFilter &
require_box_blur_filter(const patchy::SmartFilterEntry &entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::BoxBlur);
  const auto *box =
      std::get_if<patchy::BoxBlurSmartFilter>(&entry.parameters);
  CHECK(box != nullptr);
  return *box;
}

const patchy::UnsharpMaskSmartFilter &
require_unsharp_mask_filter(const patchy::SmartFilterEntry &entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::UnsharpMask);
  const auto *unsharp =
      std::get_if<patchy::UnsharpMaskSmartFilter>(&entry.parameters);
  CHECK(unsharp != nullptr);
  return *unsharp;
}

const patchy::MotionBlurSmartFilter &
require_motion_blur_filter(const patchy::SmartFilterEntry &entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::MotionBlur);
  const auto *motion =
      std::get_if<patchy::MotionBlurSmartFilter>(&entry.parameters);
  CHECK(motion != nullptr);
  return *motion;
}

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

const patchy::UnknownPsdBlock& require_placed_layer_block(const patchy::Layer& layer) {
  const auto& blocks = layer.unknown_psd_blocks();
  const auto found = std::find_if(blocks.begin(), blocks.end(), [](const patchy::UnknownPsdBlock& block) {
    return block.key == "SoLd" || block.key == "SoLE";
  });
  CHECK(found != blocks.end());
  return *found;
}

struct TestGlobalPsdBlock {
  std::size_t index{0};
  std::string key;
  bool long_length{false};
  std::vector<std::uint8_t> payload;

  bool operator==(const TestGlobalPsdBlock&) const = default;
};

std::vector<TestGlobalPsdBlock> test_global_psd_blocks(const patchy::Document& document) {
  std::vector<TestGlobalPsdBlock> result;
  const auto& metadata = document.metadata();
  result.reserve(metadata.unknown_psd_resources.size() + metadata.smart_objects.blocks.size() +
                 metadata.smart_filter_effects.blocks.size());
  for (const auto& block : metadata.unknown_psd_resources) {
    result.push_back(TestGlobalPsdBlock{block.original_global_index, block.key, block.long_length, block.payload});
  }
  for (const auto& block : metadata.smart_objects.blocks) {
    result.push_back(TestGlobalPsdBlock{block.original_global_index, block.key, block.long_length,
                                        patchy::psd::serialize_linked_layer_block(block)});
  }
  for (const auto& block : metadata.smart_filter_effects.blocks) {
    result.push_back(TestGlobalPsdBlock{block.original_global_index, block.key, block.long_length,
                                        patchy::psd::serialize_filter_effects_block(block)});
  }
  std::sort(result.begin(), result.end(), [](const TestGlobalPsdBlock& lhs, const TestGlobalPsdBlock& rhs) {
    return lhs.index < rhs.index;
  });
  return result;
}

void check_pixel_layer_storage_equal(const patchy::Layer& expected, const patchy::Layer& actual) {
  const auto expected_bounds = expected.bounds();
  const auto actual_bounds = actual.bounds();
  CHECK(expected_bounds.x == actual_bounds.x);
  CHECK(expected_bounds.y == actual_bounds.y);
  CHECK(expected_bounds.width == actual_bounds.width);
  CHECK(expected_bounds.height == actual_bounds.height);
  const auto& expected_pixels = expected.pixels();
  const auto& actual_pixels = actual.pixels();
  CHECK(expected_pixels.width() == actual_pixels.width());
  CHECK(expected_pixels.height() == actual_pixels.height());
  CHECK(patchy::bytes_per_pixel(expected_pixels.format()) ==
        patchy::bytes_per_pixel(actual_pixels.format()));
  CHECK(expected_pixels.data().size() == actual_pixels.data().size());
  CHECK(std::equal(expected_pixels.data().begin(), expected_pixels.data().end(), actual_pixels.data().begin()));
}

void psd_photoshop_high_pass_smart_filter_fixture_round_trips_and_edits() {
  const auto fixture_path = patchy::test::committed_psd_fixture_path(
      "photoshop-smart-filter-high-pass.psd");
  const auto original = patchy::psd::DocumentIo::read_file(fixture_path);
  const patchy::Layer* filtered_layer = nullptr;
  for (const auto& layer : original.layers()) {
    if (layer.smart_filter_stack() != nullptr) {
      CHECK(filtered_layer == nullptr);
      filtered_layer = &layer;
    }
  }
  CHECK(filtered_layer != nullptr);
  CHECK(patchy::layer_is_smart_object(*filtered_layer));
  CHECK(patchy::smart_object_lock_reason(*filtered_layer).empty());
  const auto* stack = filtered_layer->smart_filter_stack();
  CHECK(stack != nullptr &&
        stack->support == patchy::SmartFilterStackSupport::Supported);
  CHECK(stack->entries.size() == 1U);
  const auto& entry = stack->entries.front();
  CHECK(entry.kind == patchy::SmartFilterKind::HighPass);
  CHECK(entry.native_name == "High Pass...");
  CHECK(entry.native_class_id == "HghP");
  CHECK(entry.native_filter_id == 0x48676850U);
  CHECK(std::abs(require_high_pass_filter(entry).radius_pixels - 4.25) <
        1e-9);

  const auto original_globals = test_global_psd_blocks(original);
  const auto& original_sold = require_placed_layer_block(*filtered_layer);
  const auto original_sold_payload = original_sold.payload;
  const auto clean = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(original));
  CHECK(test_global_psd_blocks(clean) == original_globals);
  const auto& clean_layer = require_layer_named(clean, filtered_layer->name());
  check_pixel_layer_storage_equal(*filtered_layer, clean_layer);
  CHECK(require_placed_layer_block(clean_layer).payload ==
        original_sold_payload);
  CHECK(std::abs(require_high_pass_filter(
                     require_smart_filter_stack(clean, clean_layer.name())
                         .entries.front())
                     .radius_pixels -
                 4.25) < 1e-9);

  auto edited = original;
  auto* edited_layer = edited.find_layer(filtered_layer->id());
  CHECK(edited_layer != nullptr &&
        edited_layer->smart_filter_stack() != nullptr);
  auto edited_stack = *edited_layer->smart_filter_stack();
  std::get<patchy::HighPassSmartFilter>(
      edited_stack.entries.front().parameters).radius_pixels = 9.75;
  edited_stack.entries.front().opacity = 0.42;
  edited_stack.entries.front().blend_mode = patchy::BlendMode::SoftLight;
  edited_layer->set_smart_filter_stack(std::move(edited_stack));
  patchy::mark_layer_smart_object_block_dirty(*edited_layer);
  const auto edited_reread = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(edited));
  const auto& edited_reread_layer =
      require_layer_named(edited_reread, filtered_layer->name());
  const auto& edited_reread_entry =
      require_smart_filter_stack(edited_reread, filtered_layer->name())
          .entries.front();
  CHECK(std::abs(require_high_pass_filter(edited_reread_entry).radius_pixels -
                 9.75) < 1e-9);
  CHECK(std::abs(edited_reread_entry.opacity - 0.42) < 1e-9);
  CHECK(edited_reread_entry.blend_mode == patchy::BlendMode::SoftLight);
  CHECK(require_placed_layer_block(edited_reread_layer).payload !=
        original_sold_payload);
  CHECK(test_global_psd_blocks(edited_reread) == original_globals);
}

void psd_photoshop_median_smart_filter_fixture_round_trips_and_edits() {
  const auto fixture_path = patchy::test::committed_psd_fixture_path(
      "photoshop-smart-filter-median.psd");
  const auto original = patchy::psd::DocumentIo::read_file(fixture_path);
  const patchy::Layer* filtered_layer = nullptr;
  for (const auto& layer : original.layers()) {
    if (layer.smart_filter_stack() != nullptr) {
      CHECK(filtered_layer == nullptr);
      filtered_layer = &layer;
    }
  }
  CHECK(filtered_layer != nullptr);
  CHECK(patchy::layer_is_smart_object(*filtered_layer));
  CHECK(patchy::smart_object_lock_reason(*filtered_layer).empty());
  const auto* stack = filtered_layer->smart_filter_stack();
  CHECK(stack != nullptr &&
        stack->support == patchy::SmartFilterStackSupport::Supported);
  CHECK(stack->entries.size() == 1U);
  const auto& entry = stack->entries.front();
  CHECK(entry.kind == patchy::SmartFilterKind::Median);
  CHECK(entry.native_name == "Median...");
  CHECK(entry.native_class_id == "Mdn ");
  CHECK(entry.native_filter_id == 0x4d646e20U);
  CHECK(std::abs(require_median_filter(entry).radius_pixels - 7.0) < 1e-9);

  const auto original_globals = test_global_psd_blocks(original);
  const auto& original_sold = require_placed_layer_block(*filtered_layer);
  const auto original_sold_payload = original_sold.payload;
  const auto clean = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(original));
  CHECK(test_global_psd_blocks(clean) == original_globals);
  const auto& clean_layer = require_layer_named(clean, filtered_layer->name());
  check_pixel_layer_storage_equal(*filtered_layer, clean_layer);
  CHECK(require_placed_layer_block(clean_layer).payload ==
        original_sold_payload);
  CHECK(std::abs(require_median_filter(
                     require_smart_filter_stack(clean, clean_layer.name())
                         .entries.front())
                     .radius_pixels -
                 7.0) < 1e-9);

  auto edited = original;
  auto* edited_layer = edited.find_layer(filtered_layer->id());
  CHECK(edited_layer != nullptr &&
        edited_layer->smart_filter_stack() != nullptr);
  auto edited_stack = *edited_layer->smart_filter_stack();
  std::get<patchy::MedianSmartFilter>(
      edited_stack.entries.front().parameters).radius_pixels = 7.5;
  edited_stack.entries.front().opacity = 0.42;
  edited_stack.entries.front().blend_mode = patchy::BlendMode::SoftLight;
  edited_layer->set_smart_filter_stack(std::move(edited_stack));
  patchy::mark_layer_smart_object_block_dirty(*edited_layer);
  const auto edited_reread = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(edited));
  const auto& edited_reread_layer =
      require_layer_named(edited_reread, filtered_layer->name());
  const auto& edited_reread_entry =
      require_smart_filter_stack(edited_reread, filtered_layer->name())
          .entries.front();
  CHECK(std::abs(require_median_filter(edited_reread_entry).radius_pixels -
                 7.5) < 1e-9);
  CHECK(std::abs(edited_reread_entry.opacity - 0.42) < 1e-9);
  CHECK(edited_reread_entry.blend_mode == patchy::BlendMode::SoftLight);
  CHECK(require_placed_layer_block(edited_reread_layer).payload !=
        original_sold_payload);
  CHECK(test_global_psd_blocks(edited_reread) == original_globals);
}

void psd_photoshop_dust_and_scratches_smart_filter_fixture_round_trips_and_edits() {
  const auto fixture_path = patchy::test::committed_psd_fixture_path(
      "photoshop-smart-filter-dust-and-scratches.psd");
  const auto original = patchy::psd::DocumentIo::read_file(fixture_path);
  const patchy::Layer* filtered_layer = nullptr;
  for (const auto& layer : original.layers()) {
    if (layer.smart_filter_stack() != nullptr) {
      CHECK(filtered_layer == nullptr);
      filtered_layer = &layer;
    }
  }
  CHECK(filtered_layer != nullptr);
  CHECK(patchy::layer_is_smart_object(*filtered_layer));
  CHECK(patchy::smart_object_lock_reason(*filtered_layer).empty());
  const auto* stack = filtered_layer->smart_filter_stack();
  CHECK(stack != nullptr &&
        stack->support == patchy::SmartFilterStackSupport::Supported);
  CHECK(stack->entries.size() == 1U);
  const auto& entry = stack->entries.front();
  CHECK(entry.kind == patchy::SmartFilterKind::DustAndScratches);
  CHECK(entry.native_name == "Dust && Scratches...");
  CHECK(entry.native_class_id == "DstS");
  CHECK(entry.native_filter_id == 0x44737453U);
  CHECK(require_dust_and_scratches_filter(entry).radius_pixels == 7);
  CHECK(require_dust_and_scratches_filter(entry).threshold == 23);

  const auto original_globals = test_global_psd_blocks(original);
  const auto& original_sold = require_placed_layer_block(*filtered_layer);
  const auto original_sold_payload = original_sold.payload;
  const auto clean = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(original));
  CHECK(test_global_psd_blocks(clean) == original_globals);
  const auto& clean_layer = require_layer_named(clean, filtered_layer->name());
  check_pixel_layer_storage_equal(*filtered_layer, clean_layer);
  CHECK(require_placed_layer_block(clean_layer).payload ==
        original_sold_payload);
  const auto& clean_dust = require_dust_and_scratches_filter(
      require_smart_filter_stack(clean, clean_layer.name()).entries.front());
  CHECK(clean_dust.radius_pixels == 7 && clean_dust.threshold == 23);

  auto edited = original;
  auto* edited_layer = edited.find_layer(filtered_layer->id());
  CHECK(edited_layer != nullptr &&
        edited_layer->smart_filter_stack() != nullptr);
  auto edited_stack = *edited_layer->smart_filter_stack();
  auto& edited_dust = std::get<patchy::DustAndScratchesSmartFilter>(
      edited_stack.entries.front().parameters);
  edited_dust.radius_pixels = 9;
  edited_dust.threshold = 31;
  edited_stack.entries.front().opacity = 0.42;
  edited_stack.entries.front().blend_mode = patchy::BlendMode::SoftLight;
  edited_layer->set_smart_filter_stack(std::move(edited_stack));
  patchy::mark_layer_smart_object_block_dirty(*edited_layer);
  const auto edited_reread = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(edited));
  const auto& edited_reread_layer =
      require_layer_named(edited_reread, filtered_layer->name());
  const auto& edited_reread_entry =
      require_smart_filter_stack(edited_reread, filtered_layer->name())
          .entries.front();
  const auto& edited_reread_dust =
      require_dust_and_scratches_filter(edited_reread_entry);
  CHECK(edited_reread_dust.radius_pixels == 9);
  CHECK(edited_reread_dust.threshold == 31);
  CHECK(std::abs(edited_reread_entry.opacity - 0.42) < 1e-9);
  CHECK(edited_reread_entry.blend_mode == patchy::BlendMode::SoftLight);
  CHECK(require_placed_layer_block(edited_reread_layer).payload !=
        original_sold_payload);
  // Radius, threshold, opacity, and blend mode are descriptor-only edits.
  // Photoshop's unfiltered FEid cache stays byte-exact.
  CHECK(test_global_psd_blocks(edited_reread) == original_globals);
}

void psd_photoshop_surface_blur_smart_filter_fixture_round_trips_and_edits() {
  const auto fixture_path = patchy::test::committed_psd_fixture_path(
      "photoshop-smart-filter-surface-blur.psd");
  const auto original = patchy::psd::DocumentIo::read_file(fixture_path);
  const patchy::Layer* filtered_layer = nullptr;
  for (const auto& layer : original.layers()) {
    if (layer.smart_filter_stack() != nullptr) {
      CHECK(filtered_layer == nullptr);
      filtered_layer = &layer;
    }
  }
  CHECK(filtered_layer != nullptr);
  CHECK(patchy::layer_is_smart_object(*filtered_layer));
  CHECK(patchy::smart_object_lock_reason(*filtered_layer).empty());
  const auto* stack = filtered_layer->smart_filter_stack();
  CHECK(stack != nullptr &&
        stack->support == patchy::SmartFilterStackSupport::Supported);
  CHECK(stack->entries.size() == 1U);
  const auto& entry = stack->entries.front();
  CHECK(entry.kind == patchy::SmartFilterKind::SurfaceBlur);
  CHECK(entry.native_name == "Surface Blur...");
  CHECK(entry.native_class_id == "surfaceBlur");
  CHECK(entry.native_filter_id == 854U);
  CHECK(std::abs(require_surface_blur_filter(entry).radius_pixels - 9.25) <
        1e-9);
  CHECK(require_surface_blur_filter(entry).threshold == 31);

  const auto original_globals = test_global_psd_blocks(original);
  const auto& original_sold = require_placed_layer_block(*filtered_layer);
  const auto original_sold_payload = original_sold.payload;
  const auto clean = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(original));
  CHECK(test_global_psd_blocks(clean) == original_globals);
  const auto& clean_layer = require_layer_named(clean, filtered_layer->name());
  check_pixel_layer_storage_equal(*filtered_layer, clean_layer);
  CHECK(require_placed_layer_block(clean_layer).payload ==
        original_sold_payload);
  const auto& clean_surface = require_surface_blur_filter(
      require_smart_filter_stack(clean, clean_layer.name()).entries.front());
  CHECK(std::abs(clean_surface.radius_pixels - 9.25) < 1e-9);
  CHECK(clean_surface.threshold == 31);

  auto edited = original;
  auto* edited_layer = edited.find_layer(filtered_layer->id());
  CHECK(edited_layer != nullptr &&
        edited_layer->smart_filter_stack() != nullptr);
  auto edited_stack = *edited_layer->smart_filter_stack();
  auto& edited_surface = std::get<patchy::SurfaceBlurSmartFilter>(
      edited_stack.entries.front().parameters);
  edited_surface.radius_pixels = 7.5;
  edited_surface.threshold = 47;
  edited_stack.entries.front().opacity = 0.42;
  edited_stack.entries.front().blend_mode = patchy::BlendMode::SoftLight;
  edited_layer->set_smart_filter_stack(std::move(edited_stack));
  patchy::mark_layer_smart_object_block_dirty(*edited_layer);
  const auto edited_reread = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(edited));
  const auto& edited_reread_layer =
      require_layer_named(edited_reread, filtered_layer->name());
  const auto& edited_reread_entry =
      require_smart_filter_stack(edited_reread, filtered_layer->name())
          .entries.front();
  const auto& edited_reread_surface =
      require_surface_blur_filter(edited_reread_entry);
  CHECK(std::abs(edited_reread_surface.radius_pixels - 7.5) < 1e-9);
  CHECK(edited_reread_surface.threshold == 47);
  CHECK(std::abs(edited_reread_entry.opacity - 0.42) < 1e-9);
  CHECK(edited_reread_entry.blend_mode == patchy::BlendMode::SoftLight);
  CHECK(require_placed_layer_block(edited_reread_layer).payload !=
        original_sold_payload);
  // These settings live only in SoLd. The unfiltered FEid cache must remain
  // byte-exact across the descriptor edit.
  CHECK(test_global_psd_blocks(edited_reread) == original_globals);
}

void psd_photoshop_plastic_wrap_smart_filter_fixture_round_trips_and_edits() {
  const auto fixture_path = patchy::test::committed_psd_fixture_path(
      "photoshop-smart-filter-plastic-wrap.psd");
  const auto original = patchy::psd::DocumentIo::read_file(fixture_path);
  const patchy::Layer *filtered_layer = nullptr;
  for (const auto &layer : original.layers()) {
    if (layer.smart_filter_stack() != nullptr) {
      CHECK(filtered_layer == nullptr);
      filtered_layer = &layer;
    }
  }
  CHECK(filtered_layer != nullptr);
  CHECK(patchy::layer_is_smart_object(*filtered_layer));
  CHECK(patchy::smart_object_lock_reason(*filtered_layer).empty());
  const auto *stack = filtered_layer->smart_filter_stack();
  CHECK(stack != nullptr &&
        stack->support == patchy::SmartFilterStackSupport::Supported);
  CHECK(stack->entries.size() == 1U);
  const auto &entry = stack->entries.front();
  CHECK(entry.kind == patchy::SmartFilterKind::PlasticWrap);
  CHECK(entry.native_name == "Plastic Wrap...");
  CHECK(entry.native_class_id == "PlsW");
  CHECK(entry.native_filter_id == 0x506C7357U);
  CHECK(require_plastic_wrap_filter(entry).highlight_strength == 13);
  CHECK(require_plastic_wrap_filter(entry).detail == 9);
  CHECK(require_plastic_wrap_filter(entry).smoothness == 7);

  const auto original_globals = test_global_psd_blocks(original);
  const auto original_sold_payload =
      require_placed_layer_block(*filtered_layer).payload;
  const auto clean = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(original));
  CHECK(test_global_psd_blocks(clean) == original_globals);
  const auto &clean_layer =
      require_layer_named(clean, filtered_layer->name());
  check_pixel_layer_storage_equal(*filtered_layer, clean_layer);
  CHECK(require_placed_layer_block(clean_layer).payload ==
        original_sold_payload);
  const auto &clean_plastic = require_plastic_wrap_filter(
      require_smart_filter_stack(clean, clean_layer.name()).entries.front());
  CHECK(clean_plastic.highlight_strength == 13);
  CHECK(clean_plastic.detail == 9);
  CHECK(clean_plastic.smoothness == 7);

  auto edited = original;
  auto *edited_layer = edited.find_layer(filtered_layer->id());
  CHECK(edited_layer != nullptr &&
        edited_layer->smart_filter_stack() != nullptr);
  auto edited_stack = *edited_layer->smart_filter_stack();
  auto &edited_plastic = std::get<patchy::PlasticWrapSmartFilter>(
      edited_stack.entries.front().parameters);
  edited_plastic.highlight_strength = 20;
  edited_plastic.detail = 15;
  edited_plastic.smoothness = 1;
  edited_stack.entries.front().opacity = 0.42;
  edited_stack.entries.front().blend_mode = patchy::BlendMode::SoftLight;
  edited_layer->set_smart_filter_stack(std::move(edited_stack));
  patchy::mark_layer_smart_object_block_dirty(*edited_layer);
  const auto edited_reread = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(edited));
  const auto &edited_reread_layer =
      require_layer_named(edited_reread, filtered_layer->name());
  const auto &edited_reread_entry =
      require_smart_filter_stack(edited_reread, filtered_layer->name())
          .entries.front();
  const auto &edited_reread_plastic =
      require_plastic_wrap_filter(edited_reread_entry);
  CHECK(edited_reread_plastic.highlight_strength == 20);
  CHECK(edited_reread_plastic.detail == 15);
  CHECK(edited_reread_plastic.smoothness == 1);
  CHECK(std::abs(edited_reread_entry.opacity - 0.42) < 1e-9);
  CHECK(edited_reread_entry.blend_mode == patchy::BlendMode::SoftLight);
  CHECK(require_placed_layer_block(edited_reread_layer).payload !=
        original_sold_payload);
  // Plastic Wrap settings live only in SoLd. Preserve Photoshop's unfiltered
  // FEid cache exactly when only the descriptor changes.
  CHECK(test_global_psd_blocks(edited_reread) == original_globals);
}

void psd_photoshop_mosaic_smart_filter_fixture_round_trips_and_edits() {
  const auto fixture_path = patchy::test::committed_psd_fixture_path(
      "photoshop-smart-filter-mosaic.psd");
  const auto original = patchy::psd::DocumentIo::read_file(fixture_path);
  const patchy::Layer *filtered_layer = nullptr;
  for (const auto &layer : original.layers()) {
    if (layer.smart_filter_stack() != nullptr) {
      CHECK(filtered_layer == nullptr);
      filtered_layer = &layer;
    }
  }
  CHECK(filtered_layer != nullptr);
  CHECK(patchy::layer_is_smart_object(*filtered_layer));
  CHECK(patchy::smart_object_lock_reason(*filtered_layer).empty());
  const auto *stack = filtered_layer->smart_filter_stack();
  CHECK(stack != nullptr &&
        stack->support == patchy::SmartFilterStackSupport::Supported);
  CHECK(stack->entries.size() == 1U);
  const auto &entry = stack->entries.front();
  CHECK(entry.kind == patchy::SmartFilterKind::Mosaic);
  CHECK(entry.native_name == "Mosaic...");
  CHECK(entry.native_class_id == "Msc ");
  CHECK(entry.native_filter_id == 0x4d736320U);
  CHECK(require_mosaic_filter(entry).cell_size_pixels == 6);

  const auto original_globals = test_global_psd_blocks(original);
  const auto original_sold_payload =
      require_placed_layer_block(*filtered_layer).payload;
  const auto clean = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(original));
  CHECK(test_global_psd_blocks(clean) == original_globals);
  const auto &clean_layer =
      require_layer_named(clean, filtered_layer->name());
  check_pixel_layer_storage_equal(*filtered_layer, clean_layer);
  CHECK(require_placed_layer_block(clean_layer).payload ==
        original_sold_payload);
  const auto &clean_mosaic = require_mosaic_filter(
      require_smart_filter_stack(clean, clean_layer.name()).entries.front());
  CHECK(clean_mosaic.cell_size_pixels == 6);

  auto edited = original;
  auto *edited_layer = edited.find_layer(filtered_layer->id());
  CHECK(edited_layer != nullptr &&
        edited_layer->smart_filter_stack() != nullptr);
  auto edited_stack = *edited_layer->smart_filter_stack();
  auto &edited_mosaic = std::get<patchy::MosaicSmartFilter>(
      edited_stack.entries.front().parameters);
  edited_mosaic.cell_size_pixels = 24;
  edited_stack.entries.front().opacity = 0.42;
  edited_stack.entries.front().blend_mode = patchy::BlendMode::SoftLight;
  edited_layer->set_smart_filter_stack(std::move(edited_stack));
  patchy::mark_layer_smart_object_block_dirty(*edited_layer);
  const auto edited_reread = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(edited));
  const auto &edited_reread_layer =
      require_layer_named(edited_reread, filtered_layer->name());
  const auto &edited_reread_entry =
      require_smart_filter_stack(edited_reread, filtered_layer->name())
          .entries.front();
  CHECK(require_mosaic_filter(edited_reread_entry).cell_size_pixels == 24);
  CHECK(std::abs(edited_reread_entry.opacity - 0.42) < 1e-9);
  CHECK(edited_reread_entry.blend_mode == patchy::BlendMode::SoftLight);
  CHECK(require_placed_layer_block(edited_reread_layer).payload !=
        original_sold_payload);
  // Mosaic settings live only in SoLd. Preserve Photoshop's unfiltered FEid
  // cache exactly when only the descriptor changes.
  CHECK(test_global_psd_blocks(edited_reread) == original_globals);
}

void psd_photoshop_emboss_smart_filter_fixture_round_trips_and_edits() {
  const auto fixture_path = patchy::test::committed_psd_fixture_path(
      "photoshop-smart-filter-emboss.psd");
  const auto original = patchy::psd::DocumentIo::read_file(fixture_path);
  const patchy::Layer *filtered_layer = nullptr;
  for (const auto &layer : original.layers()) {
    if (layer.smart_filter_stack() != nullptr) {
      CHECK(filtered_layer == nullptr);
      filtered_layer = &layer;
    }
  }
  CHECK(filtered_layer != nullptr);
  CHECK(patchy::layer_is_smart_object(*filtered_layer));
  CHECK(patchy::smart_object_lock_reason(*filtered_layer).empty());
  const auto *stack = filtered_layer->smart_filter_stack();
  CHECK(stack != nullptr &&
        stack->support == patchy::SmartFilterStackSupport::Supported);
  CHECK(stack->entries.size() == 1U);
  const auto &entry = stack->entries.front();
  CHECK(entry.kind == patchy::SmartFilterKind::Emboss);
  CHECK(entry.native_name == "Emboss...");
  CHECK(entry.native_class_id == "Embs");
  CHECK(entry.native_filter_id == 0x456d6273U);
  CHECK(require_emboss_filter(entry).angle_degrees == 135);
  CHECK(require_emboss_filter(entry).height_pixels == 3);
  CHECK(require_emboss_filter(entry).amount_percent == 150);

  const auto original_globals = test_global_psd_blocks(original);
  const auto original_sold_payload =
      require_placed_layer_block(*filtered_layer).payload;
  const auto clean = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(original));
  CHECK(test_global_psd_blocks(clean) == original_globals);
  const auto &clean_layer =
      require_layer_named(clean, filtered_layer->name());
  check_pixel_layer_storage_equal(*filtered_layer, clean_layer);
  CHECK(require_placed_layer_block(clean_layer).payload ==
        original_sold_payload);
  const auto &clean_emboss = require_emboss_filter(
      require_smart_filter_stack(clean, clean_layer.name()).entries.front());
  CHECK(clean_emboss.angle_degrees == 135);
  CHECK(clean_emboss.height_pixels == 3);
  CHECK(clean_emboss.amount_percent == 150);

  auto edited = original;
  auto *edited_layer = edited.find_layer(filtered_layer->id());
  CHECK(edited_layer != nullptr &&
        edited_layer->smart_filter_stack() != nullptr);
  auto edited_stack = *edited_layer->smart_filter_stack();
  auto &edited_emboss = std::get<patchy::EmbossSmartFilter>(
      edited_stack.entries.front().parameters);
  edited_emboss.angle_degrees = -22;
  edited_emboss.height_pixels = 24;
  edited_emboss.amount_percent = 500;
  edited_stack.entries.front().opacity = 0.42;
  edited_stack.entries.front().blend_mode = patchy::BlendMode::SoftLight;
  edited_layer->set_smart_filter_stack(std::move(edited_stack));
  patchy::mark_layer_smart_object_block_dirty(*edited_layer);
  const auto edited_reread = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(edited));
  const auto &edited_reread_layer =
      require_layer_named(edited_reread, filtered_layer->name());
  const auto &edited_reread_entry =
      require_smart_filter_stack(edited_reread, filtered_layer->name())
          .entries.front();
  const auto &edited_reread_emboss =
      require_emboss_filter(edited_reread_entry);
  CHECK(edited_reread_emboss.angle_degrees == -22);
  CHECK(edited_reread_emboss.height_pixels == 24);
  CHECK(edited_reread_emboss.amount_percent == 500);
  CHECK(std::abs(edited_reread_entry.opacity - 0.42) < 1e-9);
  CHECK(edited_reread_entry.blend_mode == patchy::BlendMode::SoftLight);
  CHECK(require_placed_layer_block(edited_reread_layer).payload !=
        original_sold_payload);
  // Emboss settings live only in SoLd. Preserve Photoshop's unfiltered FEid
  // cache exactly when only the descriptor changes.
  CHECK(test_global_psd_blocks(edited_reread) == original_globals);
}

void psd_photoshop_box_blur_smart_filter_fixture_round_trips_and_edits() {
  const auto fixture_path = patchy::test::committed_psd_fixture_path(
      "photoshop-smart-filter-box-blur.psd");
  const auto original = patchy::psd::DocumentIo::read_file(fixture_path);
  const patchy::Layer *filtered_layer = nullptr;
  for (const auto &layer : original.layers()) {
    if (layer.smart_filter_stack() != nullptr) {
      CHECK(filtered_layer == nullptr);
      filtered_layer = &layer;
    }
  }
  CHECK(filtered_layer != nullptr);
  CHECK(patchy::layer_is_smart_object(*filtered_layer));
  CHECK(patchy::smart_object_lock_reason(*filtered_layer).empty());
  const auto *stack = filtered_layer->smart_filter_stack();
  CHECK(stack != nullptr &&
        stack->support == patchy::SmartFilterStackSupport::Supported);
  CHECK(stack->entries.size() == 1U);
  const auto &entry = stack->entries.front();
  CHECK(entry.kind == patchy::SmartFilterKind::BoxBlur);
  CHECK(entry.native_name == "Box Blur...");
  CHECK(entry.native_class_id == "boxblur");
  CHECK(entry.native_filter_id == 843U);
  CHECK(std::abs(require_box_blur_filter(entry).radius_pixels - 5.0) < 1e-9);

  const auto original_globals = test_global_psd_blocks(original);
  const auto original_sold_payload =
      require_placed_layer_block(*filtered_layer).payload;
  const auto clean = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(original));
  CHECK(test_global_psd_blocks(clean) == original_globals);
  const auto &clean_layer =
      require_layer_named(clean, filtered_layer->name());
  check_pixel_layer_storage_equal(*filtered_layer, clean_layer);
  CHECK(require_placed_layer_block(clean_layer).payload ==
        original_sold_payload);
  CHECK(std::abs(
            require_box_blur_filter(
                require_smart_filter_stack(clean, clean_layer.name())
                    .entries.front())
                .radius_pixels -
            5.0) < 1e-9);

  auto edited = original;
  auto *edited_layer = edited.find_layer(filtered_layer->id());
  CHECK(edited_layer != nullptr &&
        edited_layer->smart_filter_stack() != nullptr);
  auto edited_stack = *edited_layer->smart_filter_stack();
  auto &edited_box = std::get<patchy::BoxBlurSmartFilter>(
      edited_stack.entries.front().parameters);
  edited_box.radius_pixels = 250.5;
  edited_stack.entries.front().opacity = 0.42;
  edited_stack.entries.front().blend_mode = patchy::BlendMode::SoftLight;
  edited_layer->set_smart_filter_stack(std::move(edited_stack));
  patchy::mark_layer_smart_object_block_dirty(*edited_layer);
  const auto edited_reread = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(edited));
  const auto &edited_reread_layer =
      require_layer_named(edited_reread, filtered_layer->name());
  const auto &edited_reread_entry =
      require_smart_filter_stack(edited_reread, filtered_layer->name())
          .entries.front();
  CHECK(std::abs(require_box_blur_filter(edited_reread_entry).radius_pixels -
                 250.5) < 1e-9);
  CHECK(std::abs(edited_reread_entry.opacity - 0.42) < 1e-9);
  CHECK(edited_reread_entry.blend_mode == patchy::BlendMode::SoftLight);
  CHECK(require_placed_layer_block(edited_reread_layer).payload !=
        original_sold_payload);
  // Box Blur settings live only in SoLd. Preserve Photoshop's unfiltered FEid
  // cache exactly when only the descriptor changes.
  CHECK(test_global_psd_blocks(edited_reread) == original_globals);
}

void psd_photoshop_unsharp_motion_smart_filter_fixture_round_trips_and_edits() {
  const auto fixture_path = patchy::test::committed_psd_fixture_path(
      "photoshop-smart-filter-unsharp-motion.psd");
  const auto original = patchy::psd::DocumentIo::read_file(fixture_path);
  const auto &unsharp_layer =
      require_layer_named(original, "Unsharp Mask 175 2.5 7");
  const auto &motion_layer = require_layer_named(original, "Motion Blur 37 12");
  CHECK(patchy::smart_object_lock_reason(unsharp_layer).empty());
  CHECK(patchy::smart_object_lock_reason(motion_layer).empty());

  const auto &unsharp_stack =
      require_smart_filter_stack(original, unsharp_layer.name());
  CHECK(unsharp_stack.support == patchy::SmartFilterStackSupport::Supported);
  CHECK(unsharp_stack.entries.size() == 1U);
  const auto &unsharp_entry = unsharp_stack.entries.front();
  CHECK(unsharp_entry.native_name == "Unsharp Mask...");
  CHECK(unsharp_entry.native_class_id == "UnsM");
  CHECK(unsharp_entry.native_filter_id == 0x556e734dU);
  const auto &unsharp = require_unsharp_mask_filter(unsharp_entry);
  CHECK(std::abs(unsharp.amount_percent - 175.0) < 1e-9);
  CHECK(std::abs(unsharp.radius_pixels - 2.5) < 1e-9);
  CHECK(unsharp.threshold == 7);

  const auto &motion_stack =
      require_smart_filter_stack(original, motion_layer.name());
  CHECK(motion_stack.support == patchy::SmartFilterStackSupport::Supported);
  CHECK(motion_stack.entries.size() == 1U);
  const auto &motion_entry = motion_stack.entries.front();
  CHECK(motion_entry.native_name == "Motion Blur...");
  CHECK(motion_entry.native_class_id == "MtnB");
  CHECK(motion_entry.native_filter_id == 0x4d746e42U);
  const auto &motion = require_motion_blur_filter(motion_entry);
  CHECK(motion.angle_degrees == 37 && motion.distance_pixels == 12);

  const auto original_globals = test_global_psd_blocks(original);
  const auto unsharp_payload =
      require_placed_layer_block(unsharp_layer).payload;
  const auto motion_payload = require_placed_layer_block(motion_layer).payload;
  const auto clean = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(original));
  CHECK(test_global_psd_blocks(clean) == original_globals);
  CHECK(require_placed_layer_block(
            require_layer_named(clean, unsharp_layer.name()))
            .payload == unsharp_payload);
  CHECK(require_placed_layer_block(
            require_layer_named(clean, motion_layer.name()))
            .payload == motion_payload);

  auto edited = original;
  auto *edited_unsharp = edited.find_layer(unsharp_layer.id());
  auto *edited_motion = edited.find_layer(motion_layer.id());
  CHECK(edited_unsharp != nullptr && edited_motion != nullptr);
  auto unsharp_candidate = *edited_unsharp->smart_filter_stack();
  auto &edited_unsharp_settings = std::get<patchy::UnsharpMaskSmartFilter>(
      unsharp_candidate.entries.front().parameters);
  edited_unsharp_settings.amount_percent = 225.0;
  edited_unsharp_settings.radius_pixels = 4.75;
  edited_unsharp_settings.threshold = 11;
  edited_unsharp->set_smart_filter_stack(std::move(unsharp_candidate));
  patchy::mark_layer_smart_object_block_dirty(*edited_unsharp);

  auto motion_candidate = *edited_motion->smart_filter_stack();
  auto &edited_motion_settings = std::get<patchy::MotionBlurSmartFilter>(
      motion_candidate.entries.front().parameters);
  edited_motion_settings.angle_degrees = -61;
  edited_motion_settings.distance_pixels = 27;
  edited_motion->set_smart_filter_stack(std::move(motion_candidate));
  patchy::mark_layer_smart_object_block_dirty(*edited_motion);

  const auto reread = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(edited));
  const auto &reread_unsharp = require_unsharp_mask_filter(
      require_smart_filter_stack(reread, unsharp_layer.name()).entries.front());
  CHECK(std::abs(reread_unsharp.amount_percent - 225.0) < 1e-9);
  CHECK(std::abs(reread_unsharp.radius_pixels - 4.75) < 1e-9);
  CHECK(reread_unsharp.threshold == 11);
  const auto &reread_motion = require_motion_blur_filter(
      require_smart_filter_stack(reread, motion_layer.name()).entries.front());
  CHECK(reread_motion.angle_degrees == -61);
  CHECK(reread_motion.distance_pixels == 27);
  CHECK(test_global_psd_blocks(reread) == original_globals);
}

void psd_photoshop_tilt_shift_smart_filter_fixture_is_preserved_and_preview_locked() {
  const auto fixture_path = patchy::test::committed_psd_fixture_path(
      "photoshop-smart-filter-tilt-shift.psd");
  const auto original = patchy::psd::DocumentIo::read_file(fixture_path);
  const patchy::Layer* filtered_layer = nullptr;
  for (const auto& layer : original.layers()) {
    if (layer.smart_filter_stack() != nullptr) {
      CHECK(filtered_layer == nullptr);
      filtered_layer = &layer;
    }
  }
  CHECK(filtered_layer != nullptr);
  CHECK(patchy::layer_is_smart_object(*filtered_layer));
  CHECK(patchy::smart_object_lock_reason(*filtered_layer) == "filters");
  const auto* stack = filtered_layer->smart_filter_stack();
  CHECK(stack != nullptr);
  CHECK(stack->support == patchy::SmartFilterStackSupport::Unsupported);
  CHECK(stack->entries.size() == 1U);
  const auto& entry = stack->entries.front();
  CHECK(entry.kind == patchy::SmartFilterKind::Unsupported);
  CHECK(entry.native_name == "Blur Gallery...");
  CHECK(entry.native_class_id == "blurbTransform");
  CHECK(entry.native_filter_id == 712U);

  const auto original_globals = test_global_psd_blocks(original);
  const auto original_sold_payload =
      require_placed_layer_block(*filtered_layer).payload;
  const auto serialized = patchy::psd::DocumentIo::write_layered_rgb8(original);
  const auto clean = patchy::psd::DocumentIo::read(serialized);
  CHECK(test_global_psd_blocks(clean) == original_globals);
  const auto& clean_layer = require_layer_named(clean, filtered_layer->name());
  check_pixel_layer_storage_equal(*filtered_layer, clean_layer);
  CHECK(require_placed_layer_block(clean_layer).payload ==
        original_sold_payload);
  CHECK(patchy::smart_object_lock_reason(clean_layer) == "filters");
  const auto& clean_stack =
      require_smart_filter_stack(clean, clean_layer.name());
  CHECK(clean_stack.support == patchy::SmartFilterStackSupport::Unsupported);
  CHECK(clean_stack.entries.size() == 1U);
  CHECK(clean_stack.entries.front().kind ==
        patchy::SmartFilterKind::Unsupported);
  CHECK(clean_stack.entries.front().native_class_id == "blurbTransform");
  CHECK(clean_stack.entries.front().native_filter_id == 712U);
  CHECK(patchy::psd::DocumentIo::write_layered_rgb8(clean) == serialized);

  std::filesystem::create_directories("test-artifacts");
  const auto acceptance_path = std::filesystem::path("test-artifacts") /
                               "patchy-smart-filter-tilt-shift-resaved.psd";
  patchy::psd::DocumentIo::write_layered_rgb8_file(original,
                                                    acceptance_path);
  CHECK(std::filesystem::exists(acceptance_path));
}

void psd_smart_filter_descriptor_semantics_parse_if_available() {
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-smart-filter-model.psd"));
  CHECK(document.metadata().smart_filter_effects.blocks.size() == 1U);
  const auto& effects_block = document.metadata().smart_filter_effects.blocks.front();
  CHECK(effects_block.key == "FEid");
  CHECK(effects_block.version == 3U);
  CHECK(!effects_block.opaque);
  CHECK(effects_block.records.size() == 11U);
  CHECK(effects_block.original_payload != nullptr);

  const auto& radius_layer = require_layer_named(document, "Gaussian radius 2.0");
  CHECK(patchy::smart_object_lock_reason(radius_layer).empty());
  CHECK(!patchy::smart_object_placed_uuid(radius_layer).empty());
  const auto& stack = require_smart_filter_stack(document, "Gaussian radius 2.0");
  CHECK(stack.enabled);
  CHECK(stack.valid_at_position);
  CHECK(stack.entries.size() == 1U);
  CHECK(stack.support == patchy::SmartFilterStackSupport::Supported);
  const auto& entry = stack.entries.front();
  CHECK(entry.kind == patchy::SmartFilterKind::GaussianBlur);
  CHECK(entry.native_name == "Gaussian Blur...");
  CHECK(entry.native_class_id == "GsnB");
  CHECK(entry.native_filter_id == 0x47736e42U);
  CHECK(entry.enabled);
  CHECK(entry.has_options);
  CHECK(std::abs(entry.opacity - 1.0) < 1e-9);
  CHECK(entry.blend_mode == patchy::BlendMode::Normal);
  CHECK(entry.foreground.red == 0 && entry.foreground.green == 0 && entry.foreground.blue == 0);
  CHECK(entry.background.red == 255 && entry.background.green == 255 && entry.background.blue == 255);
  CHECK(std::abs(require_gaussian_filter(entry).radius_pixels - 2.0) < 1e-9);

  const auto& fractional = require_smart_filter_stack(document, "Gaussian radius 2.5 fractional");
  CHECK(fractional.support == patchy::SmartFilterStackSupport::Supported);
  CHECK(std::abs(require_gaussian_filter(fractional.entries.front()).radius_pixels - 2.5) < 1e-9);

  const auto& disabled_entry = require_smart_filter_stack(document, "Gaussian disabled");
  CHECK(disabled_entry.enabled);
  CHECK(!disabled_entry.entries.front().enabled);
  CHECK(std::abs(require_gaussian_filter(disabled_entry.entries.front()).radius_pixels - 3.0) < 1e-9);

  const auto& disabled_root = require_smart_filter_stack(document, "All Smart Filters disabled");
  CHECK(!disabled_root.enabled);
  CHECK(disabled_root.entries.front().enabled);
  CHECK(std::abs(require_gaussian_filter(disabled_root.entries.front()).radius_pixels - 4.0) < 1e-9);

  const auto& multiply = require_smart_filter_stack(document, "Gaussian Multiply 37 percent");
  CHECK(multiply.entries.front().blend_mode == patchy::BlendMode::Multiply);
  CHECK(std::abs(multiply.entries.front().opacity - 0.37) < 1e-9);
  CHECK(std::abs(require_gaussian_filter(multiply.entries.front()).radius_pixels - 1.5) < 1e-9);

  const auto& median_then_gaussian =
      require_smart_filter_stack(document, "Applied Median then Gaussian");
  CHECK(median_then_gaussian.entries.size() == 2U);
  CHECK(median_then_gaussian.entries[0].native_class_id == "Mdn ");
  CHECK(median_then_gaussian.entries[0].kind ==
        patchy::SmartFilterKind::Median);
  CHECK(std::abs(require_median_filter(median_then_gaussian.entries[0])
                     .radius_pixels -
                 2.0) < 1e-9);
  CHECK(median_then_gaussian.entries[1].native_class_id == "GsnB");
  CHECK(median_then_gaussian.entries[1].kind == patchy::SmartFilterKind::GaussianBlur);
  CHECK(median_then_gaussian.support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(patchy::smart_object_lock_reason(
            require_layer_named(document, "Applied Median then Gaussian"))
            .empty());

  const auto& gaussian_then_median =
      require_smart_filter_stack(document, "Applied Gaussian then Median");
  CHECK(gaussian_then_median.entries.size() == 2U);
  CHECK(gaussian_then_median.entries[0].native_class_id == "GsnB");
  CHECK(gaussian_then_median.entries[1].native_class_id == "Mdn ");
  CHECK(gaussian_then_median.entries[1].kind ==
        patchy::SmartFilterKind::Median);
  CHECK(std::abs(require_median_filter(gaussian_then_median.entries[1])
                     .radius_pixels -
                 2.0) < 1e-9);
  CHECK(gaussian_then_median.support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(patchy::smart_object_lock_reason(
            require_layer_named(document, "Applied Gaussian then Median"))
            .empty());

  for (const auto& layer : document.layers()) {
    if (const auto* layer_stack = layer.smart_filter_stack(); layer_stack != nullptr) {
      if (layer_stack->support == patchy::SmartFilterStackSupport::Supported) {
        CHECK(patchy::smart_object_lock_reason(layer).empty());
      } else {
        CHECK(patchy::smart_object_lock_reason(layer) == "filters");
      }
    }
  }
}

void psd_smart_filter_masks_decode_and_coexist_with_layer_mask() {
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-smart-filter-model.psd"));
  const auto& layered = require_layer_named(document, "Layer mask plus Smart Filter mask");
  CHECK(layered.mask().has_value());
  const auto& layered_stack = require_smart_filter_stack(document, layered.name());
  CHECK(!layered_stack.mask.pixels.empty());
  CHECK(layered_stack.mask.bounds.width == 40 && layered_stack.mask.bounds.height == 40);
  CHECK(layered.mask()->pixels.data().data() != layered_stack.mask.pixels.data().data());
  CHECK(layered_stack.mask.pixels.pixel(3, 20)[0] == 0);
  CHECK(layered_stack.mask.pixels.pixel(4, 20)[0] == 255);
  CHECK(layered_stack.mask.pixels.pixel(35, 20)[0] == 255);
  CHECK(layered_stack.mask.pixels.pixel(36, 20)[0] == 0);
  CHECK(std::count(layered_stack.mask.pixels.data().begin(),
                   layered_stack.mask.pixels.data().end(),
                   static_cast<std::uint8_t>(255)) == 32 * 32);

  const auto& hard = require_smart_filter_stack(document, "Selection-derived hard filter mask").mask;
  CHECK(hard.enabled);
  CHECK(!hard.linked);
  CHECK(!hard.extend_with_white);
  CHECK(hard.bounds.x == 0 && hard.bounds.y == 0 && hard.bounds.width == 40 && hard.bounds.height == 40);
  CHECK(hard.pixels.width() == 40 && hard.pixels.height() == 40);
  CHECK(hard.pixels.pixel(4, 20)[0] == 0);
  CHECK(hard.pixels.pixel(5, 20)[0] == 255);
  CHECK(hard.pixels.pixel(31, 20)[0] == 255);
  CHECK(hard.pixels.pixel(32, 20)[0] == 0);
  CHECK(hard.pixels.pixel(20, 3)[0] == 0);
  CHECK(hard.pixels.pixel(20, 4)[0] == 255);
  CHECK(hard.pixels.pixel(20, 33)[0] == 255);
  CHECK(hard.pixels.pixel(20, 34)[0] == 0);
  CHECK(std::count(hard.pixels.data().begin(), hard.pixels.data().end(),
                   static_cast<std::uint8_t>(255)) == 27 * 30);
  CHECK(std::count(hard.pixels.data().begin(), hard.pixels.data().end(),
                   static_cast<std::uint8_t>(0)) == 40 * 40 - 27 * 30);

  const auto& five_tone = require_smart_filter_stack(
      document, "Five-tone filter mask 0 64 128 192 255").mask;
  CHECK(five_tone.enabled);
  CHECK(!five_tone.linked);
  CHECK(!five_tone.extend_with_white);
  constexpr std::array<std::uint8_t, 5> expected{0, 64, 128, 192, 255};
  for (std::size_t band = 0; band < expected.size(); ++band) {
    const auto x = static_cast<std::int32_t>(band * 8U);
    CHECK(five_tone.pixels.pixel(x, 20)[0] == expected[band]);
    CHECK(five_tone.pixels.pixel(x + 7, 20)[0] == expected[band]);
  }

  const auto& disabled = require_smart_filter_stack(document, "Disabled selection filter mask").mask;
  CHECK(!disabled.enabled);
  CHECK(disabled.pixels.pixel(2, 20)[0] == 0);
  CHECK(disabled.pixels.pixel(3, 20)[0] == 255);
  CHECK(disabled.pixels.pixel(33, 20)[0] == 255);
  CHECK(disabled.pixels.pixel(34, 20)[0] == 0);
  CHECK(disabled.pixels.pixel(20, 7)[0] == 0);
  CHECK(disabled.pixels.pixel(20, 8)[0] == 255);
  CHECK(disabled.pixels.pixel(20, 36)[0] == 255);
  CHECK(disabled.pixels.pixel(20, 37)[0] == 0);
  CHECK(std::count(disabled.pixels.data().begin(), disabled.pixels.data().end(),
                   static_cast<std::uint8_t>(255)) == 31 * 29);
  CHECK(std::count(disabled.pixels.data().begin(), disabled.pixels.data().end(),
                   static_cast<std::uint8_t>(0)) == 40 * 40 - 31 * 29);
}

void psd_smart_filter_instances_have_independent_native_records() {
  const auto base = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-smart-filter-instances-base.psd"));
  const auto& instance_a = require_layer_named(base, "Instance A Gaussian 1.5");
  const auto& instance_b = require_layer_named(base, "Instance B Gaussian 4.5 transformed masked");
  CHECK(patchy::smart_object_source_uuid(instance_a) == patchy::smart_object_source_uuid(instance_b));
  CHECK(!patchy::smart_object_source_uuid(instance_a).empty());
  const auto placed_a = patchy::smart_object_placed_uuid(instance_a);
  const auto placed_b = patchy::smart_object_placed_uuid(instance_b);
  CHECK(!placed_a.empty() && !placed_b.empty() && placed_a != placed_b);
  CHECK(base.metadata().smart_filter_effects.blocks.size() == 1U);
  CHECK(base.metadata().smart_filter_effects.blocks.front().records.size() == 2U);
  const auto* record_a = base.metadata().smart_filter_effects.find_unique(placed_a);
  const auto* record_b = base.metadata().smart_filter_effects.find_unique(placed_b);
  CHECK(record_a != nullptr && record_b != nullptr);
  CHECK(record_a->semantic_supported() && record_b->semantic_supported());
  CHECK(record_a->raw_storage == record_b->raw_storage);
  CHECK(record_a->mask.has_value() && record_b->mask.has_value());
  CHECK(std::abs(require_gaussian_filter(require_smart_filter_stack(base, instance_a.name()).entries.front())
                     .radius_pixels -
                 1.5) < 1e-9);
  const auto& stack_b = require_smart_filter_stack(base, instance_b.name());
  CHECK(std::abs(require_gaussian_filter(stack_b.entries.front()).radius_pixels - 4.5) < 1e-9);
  CHECK(stack_b.mask.pixels.pixel(0, 20)[0] == 0);
  CHECK(stack_b.mask.pixels.pixel(8, 20)[0] == 64);
  CHECK(stack_b.mask.pixels.pixel(16, 20)[0] == 128);
  CHECK(stack_b.mask.pixels.pixel(24, 20)[0] == 192);
  CHECK(stack_b.mask.pixels.pixel(32, 20)[0] == 255);

  const auto rasterized = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-smart-filter-instances-rasterized.psd"));
  const auto& raster_a = require_layer_named(rasterized, "Instance A Gaussian 1.5");
  const auto& raster_b = require_layer_named(rasterized, "Instance B rasterized from filtered object");
  CHECK(patchy::smart_object_source_uuid(raster_a) == patchy::smart_object_source_uuid(instance_a));
  CHECK(patchy::layer_is_smart_object(raster_a));
  CHECK(!patchy::layer_is_smart_object(raster_b));
  CHECK(raster_b.smart_filter_stack() == nullptr);
  CHECK(rasterized.metadata().smart_filter_effects.blocks.size() == 1U);
  CHECK(rasterized.metadata().smart_filter_effects.blocks.front().records.size() == 1U);
  CHECK(rasterized.metadata().smart_filter_effects.find_unique(placed_a) != nullptr);
  CHECK(rasterized.metadata().smart_filter_effects.find_unique(placed_b) == nullptr);
  CHECK(rasterized.metadata().smart_objects.find(patchy::smart_object_source_uuid(instance_a)) != nullptr);
}

void psd_smart_filter_clean_resave_preserves_native_data_and_pixels() {
  const auto original = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-smart-filter-model.psd"));
  const auto original_globals = test_global_psd_blocks(original);
  const auto written = patchy::psd::DocumentIo::write_layered_rgb8(original);
  const auto reread = patchy::psd::DocumentIo::read(written);
  CHECK(test_global_psd_blocks(reread) == original_globals);
  CHECK(reread.layers().size() == original.layers().size());
  for (const auto& original_layer : original.layers()) {
    const auto& reread_layer = require_layer_named(reread, original_layer.name());
    check_pixel_layer_storage_equal(original_layer, reread_layer);
    if (patchy::layer_is_smart_object(original_layer)) {
      const auto& original_sold = require_placed_layer_block(original_layer);
      const auto& reread_sold = require_placed_layer_block(reread_layer);
      CHECK(original_sold.key == reread_sold.key);
      CHECK(original_sold.long_length == reread_sold.long_length);
      CHECK(original_sold.payload == reread_sold.payload);
    }
  }
  std::filesystem::create_directories("test-artifacts");
  const auto clean_com_path =
      std::filesystem::path("test-artifacts") / "patchy-smart-filter-clean-com.psd";
  patchy::psd::DocumentIo::write_layered_rgb8_file(original, clean_com_path);
  CHECK(std::filesystem::exists(clean_com_path));
  const auto clean_com_reread = patchy::psd::DocumentIo::read_file(clean_com_path);
  CHECK(test_global_psd_blocks(clean_com_reread) == original_globals);

  // Removing every filtered instance drops FEid but leaves the order of all
  // surviving document-global blocks unchanged.
  auto stripped = original;
  std::vector<patchy::LayerId> filtered_ids;
  for (const auto& layer : stripped.layers()) {
    if (layer.smart_filter_stack() != nullptr) {
      filtered_ids.push_back(layer.id());
    }
  }
  for (const auto id : filtered_ids) {
    CHECK(stripped.remove_layer(id));
  }
  CHECK(stripped.metadata().smart_filter_effects.empty());
  auto expected_surviving_globals = original_globals;
  expected_surviving_globals.erase(
      std::remove_if(expected_surviving_globals.begin(), expected_surviving_globals.end(),
                     [](const TestGlobalPsdBlock& block) { return block.key == "FEid" || block.key == "FXid"; }),
      expected_surviving_globals.end());
  const auto stripped_reread = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(stripped));
  const auto actual_surviving_globals = test_global_psd_blocks(stripped_reread);
  CHECK(actual_surviving_globals.size() == expected_surviving_globals.size());
  for (std::size_t i = 0; i < actual_surviving_globals.size(); ++i) {
    CHECK(actual_surviving_globals[i].key == expected_surviving_globals[i].key);
    CHECK(actual_surviving_globals[i].long_length == expected_surviving_globals[i].long_length);
  }
}

void smart_filter_effects_codec_rejects_malformed_data_and_accepts_alignment() {
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-smart-filter-model.psd"));
  const auto& source_record = document.metadata().smart_filter_effects.blocks.front().records.front();
  const auto body = patchy::psd::raw_filter_effects_record_body(source_record);
  CHECK(!body.empty());
  const auto make_payload = [&](std::size_t tail_count, std::uint8_t tail_value,
                                bool duplicate = false) {
    patchy::psd::BigEndianWriter writer;
    writer.write_u32(3U);
    const auto write_record = [&]() {
      writer.write_u64(static_cast<std::uint64_t>(body.size()));
      writer.write_bytes(body);
      while ((writer.bytes().size() % 4U) != 0U) {
        writer.write_u8(0U);
      }
    };
    write_record();
    if (duplicate) {
      write_record();
    }
    for (std::size_t i = 0; i < tail_count; ++i) {
      writer.write_u8(tail_value);
    }
    return writer.bytes();
  };

  for (std::size_t zero_tail = 0; zero_tail <= 3U; ++zero_tail) {
    const auto payload = make_payload(zero_tail, 0U);
    const auto parsed = patchy::psd::parse_filter_effects_block("FEid", payload, false, 17U);
    CHECK(!parsed.opaque);
    CHECK(parsed.original_global_index == 17U);
    CHECK(parsed.records.size() == 1U);
    CHECK(parsed.records.front().semantic_supported());
    CHECK(patchy::psd::serialize_filter_effects_block(parsed) == payload);
  }

  for (const auto payload : {make_payload(1U, 1U), make_payload(4U, 0U)}) {
    const auto parsed = patchy::psd::parse_filter_effects_block("FEid", payload);
    CHECK(parsed.opaque);
    CHECK(parsed.records.empty());
    CHECK(patchy::psd::serialize_filter_effects_block(parsed) == payload);
  }

  auto oversized = make_payload(0U, 0U);
  CHECK(oversized.size() > 12U);
  std::fill(oversized.begin() + 4, oversized.begin() + 12,
            static_cast<std::uint8_t>(0xffU));
  const auto malformed_outer = patchy::psd::parse_filter_effects_block("FEid", oversized);
  CHECK(malformed_outer.opaque);
  CHECK(malformed_outer.records.empty());
  CHECK(patchy::psd::serialize_filter_effects_block(malformed_outer) == oversized);

  auto unsupported_body = std::vector<std::uint8_t>(body.begin(), body.end());
  patchy::psd::BigEndianReader record_reader(unsupported_body);
  const auto id_length = record_reader.read_u8();
  record_reader.skip(id_length);
  CHECK(record_reader.read_u32() == 1U);
  const auto cache_length = record_reader.read_u64();
  record_reader.skip(static_cast<std::size_t>(cache_length));
  CHECK(record_reader.read_u8() == 1U);
  record_reader.skip(16U);
  const auto mask_length = record_reader.read_u64();
  CHECK(mask_length >= 2U && mask_length <= record_reader.remaining());
  const auto compression_offset = record_reader.position();
  unsupported_body[compression_offset] = 0U;
  unsupported_body[compression_offset + 1U] = 2U;
  patchy::psd::BigEndianWriter unsupported_writer;
  unsupported_writer.write_u32(3U);
  unsupported_writer.write_u64(static_cast<std::uint64_t>(unsupported_body.size()));
  unsupported_writer.write_bytes(unsupported_body);
  const auto unsupported_payload = unsupported_writer.bytes();
  const auto unsupported = patchy::psd::parse_filter_effects_block("FEid", unsupported_payload);
  CHECK(!unsupported.opaque);
  CHECK(unsupported.records.size() == 1U);
  CHECK(!unsupported.records.front().data_supported);
  CHECK(unsupported.records.front().association_unique);
  CHECK(!unsupported.records.front().semantic_supported());
  CHECK(patchy::psd::serialize_filter_effects_block(unsupported) == unsupported_payload);

  const auto write_u32_at = [](std::vector<std::uint8_t>& bytes,
                               std::size_t offset, std::uint32_t value) {
    CHECK(offset + 4U <= bytes.size());
    bytes[offset + 0U] = static_cast<std::uint8_t>(value >> 24U);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 16U);
    bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value);
  };
  const auto write_u64_at = [](std::vector<std::uint8_t>& bytes,
                               std::size_t offset, std::uint64_t value) {
    CHECK(offset + 8U <= bytes.size());
    for (std::size_t index = 0; index < 8U; ++index) {
      bytes[offset + index] = static_cast<std::uint8_t>(
          value >> ((7U - index) * 8U));
    }
  };
  const auto wrap_record_body = [](std::span<const std::uint8_t> record_body) {
    patchy::psd::BigEndianWriter writer;
    writer.write_u32(3U);
    writer.write_u64(static_cast<std::uint64_t>(record_body.size()));
    writer.write_bytes(record_body);
    return writer.bytes();
  };

  // A malformed PackBits row must not hide extra bytes after producing the
  // requested width. FEid row lengths are exact record boundaries.
  auto trailing_row_body = std::vector<std::uint8_t>(body.begin(), body.end());
  patchy::psd::BigEndianReader trailing_reader(trailing_row_body);
  const auto trailing_id_length = trailing_reader.read_u8();
  trailing_reader.skip(trailing_id_length);
  CHECK(trailing_reader.read_u32() == 1U);
  const auto trailing_cache_length = trailing_reader.read_u64();
  trailing_reader.skip(static_cast<std::size_t>(trailing_cache_length));
  CHECK(trailing_reader.read_u8() == 1U);
  const auto mask_top = static_cast<std::int32_t>(trailing_reader.read_u32());
  (void)trailing_reader.read_u32();
  const auto mask_bottom = static_cast<std::int32_t>(trailing_reader.read_u32());
  (void)trailing_reader.read_u32();
  CHECK(mask_bottom > mask_top);
  const auto mask_height = static_cast<std::size_t>(mask_bottom - mask_top);
  const auto mask_length_offset = trailing_reader.position();
  const auto trailing_mask_length = trailing_reader.read_u64();
  const auto mask_body_offset = trailing_reader.position();
  CHECK(trailing_reader.read_u16() == 1U);
  const auto row_table_offset = trailing_reader.position();
  const auto first_row_length = trailing_reader.read_u32();
  const auto encoded_rows_offset = mask_body_offset + 2U + mask_height * 4U;
  const auto first_row_end = encoded_rows_offset + first_row_length;
  CHECK(first_row_end <= mask_body_offset + trailing_mask_length);
  trailing_row_body.insert(
      trailing_row_body.begin() + static_cast<std::ptrdiff_t>(first_row_end),
      0U);
  write_u32_at(trailing_row_body, row_table_offset, first_row_length + 1U);
  write_u64_at(trailing_row_body, mask_length_offset,
               trailing_mask_length + 1U);
  const auto trailing_row = patchy::psd::parse_filter_effects_block(
      "FEid", wrap_record_body(trailing_row_body));
  CHECK(!trailing_row.opaque);
  CHECK(trailing_row.records.size() == 1U);
  CHECK(!trailing_row.records.front().data_supported);
  CHECK(trailing_row.records.front().mask_present);
  CHECK(!trailing_row.records.front().mask_decoded);

  // Once the cache layout is invalid, mask parsing/decompression is skipped.
  // This keeps a bad cache from spending the mask's bounded allocation budget.
  auto bad_cache_body = std::vector<std::uint8_t>(body.begin(), body.end());
  patchy::psd::BigEndianReader cache_reader(bad_cache_body);
  const auto cache_id_length = cache_reader.read_u8();
  cache_reader.skip(cache_id_length);
  CHECK(cache_reader.read_u32() == 1U);
  (void)cache_reader.read_u64();
  const auto cache_body_offset = cache_reader.position();
  write_u32_at(bad_cache_body, cache_body_offset + 16U, 16U);
  const auto bad_cache = patchy::psd::parse_filter_effects_block(
      "FEid", wrap_record_body(bad_cache_body));
  CHECK(!bad_cache.opaque);
  CHECK(bad_cache.records.size() == 1U);
  CHECK(!bad_cache.records.front().data_supported);
  CHECK(!bad_cache.records.front().mask_present);
  CHECK(!bad_cache.records.front().mask_decoded);

  const auto duplicate_payload = make_payload(0U, 0U, true);
  auto duplicate_block = patchy::psd::parse_filter_effects_block("FEid", duplicate_payload);
  CHECK(!duplicate_block.opaque);
  CHECK(duplicate_block.records.size() == 2U);
  CHECK(!duplicate_block.records[0].association_unique);
  CHECK(!duplicate_block.records[1].association_unique);
  CHECK(patchy::psd::serialize_filter_effects_block(duplicate_block) == duplicate_payload);
  patchy::SmartFilterEffectsStore duplicate_store;
  duplicate_store.add_block(std::move(duplicate_block));
  CHECK(duplicate_store.find_unique(source_record.placed_uuid) == nullptr);

  // Patchy builds before 0.20 wrote adjacent records without Photoshop's
  // per-record alignment. Keep those files readable and byte-preserved while
  // every newly rebuilt block uses the native aligned shape.
  const auto unaligned_document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path(
          "photoshop-smart-filter-unsharp-motion.psd"));
  const auto &unaligned_source = unaligned_document.metadata()
                                     .smart_filter_effects.blocks.front()
                                     .records.front();
  const auto unaligned_body =
      patchy::psd::raw_filter_effects_record_body(unaligned_source);
  CHECK((4U + 8U + unaligned_body.size()) % 4U != 0U);
  patchy::psd::BigEndianWriter legacy_writer;
  legacy_writer.write_u32(3U);
  for (int record = 0; record < 2; ++record) {
    legacy_writer.write_u64(static_cast<std::uint64_t>(unaligned_body.size()));
    legacy_writer.write_bytes(unaligned_body);
  }
  while ((legacy_writer.bytes().size() % 4U) != 0U) {
    legacy_writer.write_u8(0U);
  }
  const auto legacy_payload = legacy_writer.bytes();
  const auto legacy_block =
      patchy::psd::parse_filter_effects_block("FEid", legacy_payload);
  CHECK(!legacy_block.opaque && legacy_block.records.size() == 2U);
  CHECK(patchy::psd::serialize_filter_effects_block(legacy_block) ==
        legacy_payload);

  // An opaque FEid/FXid could contain a duplicate association that cannot be
  // proven absent. Its presence therefore makes every parsed association and
  // every mutation fail closed.
  auto valid_block = patchy::psd::parse_filter_effects_block(
      "FEid", make_payload(0U, 0U));
  const auto valid_record = valid_block.records.front();
  patchy::psd::BigEndianWriter opaque_writer;
  opaque_writer.write_u32(99U);
  const auto opaque_payload = opaque_writer.bytes();
  auto opaque_block = patchy::psd::parse_filter_effects_block(
      "FXid", opaque_payload);
  CHECK(opaque_block.opaque);
  patchy::SmartFilterEffectsStore opaque_store;
  opaque_store.add_block(std::move(valid_block));
  opaque_store.add_block(std::move(opaque_block));
  CHECK(opaque_store.find_unique(valid_record.placed_uuid) == nullptr);
  CHECK(!opaque_store.blocks.front().records.front().association_unique);
  CHECK(!opaque_store.clone_rekey(
      valid_record.placed_uuid,
      "21234567-89ab-cdef-8123-456789abcdef"));
  CHECK(!opaque_store.adopt(
      valid_record, "31234567-89ab-cdef-8123-456789abcdef"));
  CHECK(!opaque_store.remove(valid_record.placed_uuid));
}

void smart_filter_effects_associations_fail_closed_and_preserve_raw_payload() {
  const auto fixture_path =
      patchy::test::committed_psd_fixture_path("photoshop-smart-filter-model.psd");
  const auto target_name = std::string("Gaussian radius 2.5 fractional");

  auto duplicated = patchy::psd::DocumentIo::read_file(fixture_path);
  const auto duplicate_uuid = patchy::smart_object_placed_uuid(require_layer_named(duplicated, target_name));
  const auto* original_record = duplicated.metadata().smart_filter_effects.find_unique(duplicate_uuid);
  CHECK(original_record != nullptr);
  const auto copied_record = *original_record;
  auto& duplicate_block = duplicated.metadata().smart_filter_effects.blocks.front();
  duplicate_block.original_payload.reset();
  duplicate_block.records.push_back(copied_record);
  const auto duplicate_read = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(duplicated));
  CHECK(duplicate_read.metadata().smart_filter_effects.find_unique(duplicate_uuid) == nullptr);
  CHECK(require_smart_filter_stack(duplicate_read, target_name).support ==
        patchy::SmartFilterStackSupport::Unsupported);
  CHECK(patchy::smart_object_lock_reason(require_layer_named(duplicate_read, target_name)) == "filters");
  const auto duplicate_payload =
      *duplicate_read.metadata().smart_filter_effects.blocks.front().original_payload;
  const auto duplicate_again = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(duplicate_read));
  CHECK(*duplicate_again.metadata().smart_filter_effects.blocks.front().original_payload == duplicate_payload);

  auto missing = patchy::psd::DocumentIo::read_file(fixture_path);
  const auto missing_uuid = patchy::smart_object_placed_uuid(require_layer_named(missing, target_name));
  CHECK(missing.metadata().smart_filter_effects.remove(missing_uuid));
  const auto missing_read = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(missing));
  CHECK(missing_read.metadata().smart_filter_effects.find_unique(missing_uuid) == nullptr);
  CHECK(require_smart_filter_stack(missing_read, target_name).support ==
        patchy::SmartFilterStackSupport::Unsupported);
  const auto missing_payload = *missing_read.metadata().smart_filter_effects.blocks.front().original_payload;
  const auto missing_again = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(missing_read));
  CHECK(*missing_again.metadata().smart_filter_effects.blocks.front().original_payload == missing_payload);
}

void smart_filter_unsupported_clone_preserves_filterfx_and_rekeys_cache() {
  const auto fixture_path =
      patchy::test::committed_psd_fixture_path("photoshop-smart-filter-model.psd");
  auto document = patchy::psd::DocumentIo::read_file(fixture_path);
  const std::string source_name = "Applied Gaussian then Median";
  const std::string clone_name = "Applied Gaussian then Median copy";
  auto* descriptor_layer = document.find_layer(
      require_layer_named(document, source_name).id());
  CHECK(descriptor_layer != nullptr);
  constexpr std::array<std::uint8_t, 4> median_id{'M', 'd', 'n', ' '};
  constexpr std::array<std::uint8_t, 4> unknown_id{'Z', 'Z', 'Z', 'Z'};
  std::size_t replacements = 0U;
  for (auto& block : descriptor_layer->unknown_psd_blocks()) {
    if (block.key != "SoLd" && block.key != "SoLE") {
      continue;
    }
    auto begin = block.payload.begin();
    while (begin != block.payload.end()) {
      const auto found = std::search(begin, block.payload.end(),
                                     median_id.begin(), median_id.end());
      if (found == block.payload.end()) {
        break;
      }
      std::copy(unknown_id.begin(), unknown_id.end(), found);
      ++replacements;
      begin = found + static_cast<std::ptrdiff_t>(unknown_id.size());
    }
  }
  CHECK(replacements >= 1U);
  document = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto& source = require_layer_named(document, source_name);
  const auto source_snapshot = source;
  const auto source_placed_uuid = patchy::smart_object_placed_uuid(source);
  const auto source_object_uuid = patchy::smart_object_source_uuid(source);
  CHECK(!source_placed_uuid.empty() && !source_object_uuid.empty());
  CHECK(require_smart_filter_stack(document, source_name).support ==
        patchy::SmartFilterStackSupport::Unsupported);
  CHECK(patchy::smart_object_lock_reason(source) == "filters");
  CHECK(document.metadata().smart_filter_effects.find_unique(source_placed_uuid) != nullptr);

  const auto filter_fx_bytes = [](const patchy::Layer& layer) {
    const auto& sold = require_placed_layer_block(layer);
    patchy::psd::BigEndianReader reader(sold.payload);
    CHECK(patchy::psd::key_string(patchy::psd::read_signature(reader)) == "soLD");
    (void)reader.read_u32();
    (void)reader.read_u32();
    const auto descriptor = patchy::psd::read_descriptor(reader);
    const auto* filter_fx = patchy::psd::descriptor_value(descriptor, "filterFX");
    CHECK(filter_fx != nullptr);
    patchy::psd::BigEndianWriter writer;
    patchy::psd::write_descriptor_value(writer, *filter_fx);
    return writer.bytes();
  };
  const auto original_filter_fx = filter_fx_bytes(source);
  CHECK(!original_filter_fx.empty());

  const std::string clone_placed_uuid =
      "c1234567-89ab-cdef-8123-456789abcdef";
  auto clone = source.clone_with_id(document.allocate_layer_id());
  clone.set_name(clone_name);
  patchy::set_photoshop_layer_id(
      clone, patchy::next_photoshop_layer_id(document.layers()));
  CHECK(document.metadata().smart_filter_effects.clone_rekey(
      source_placed_uuid, clone_placed_uuid));
  clone.metadata()[patchy::kLayerMetadataSmartObjectPlaced] =
      clone_placed_uuid;
  patchy::mark_layer_smart_object_block_dirty(clone);
  CHECK(patchy::layer_smart_object_block_dirty(clone));
  CHECK(patchy::smart_object_placed_uuid(clone) == clone_placed_uuid);
  document.add_layer(std::move(clone));

  const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto reread = patchy::psd::DocumentIo::read(written);
  const auto& reread_source = require_layer_named(reread, source_name);
  const auto& reread_clone = require_layer_named(reread, clone_name);
  CHECK(patchy::smart_object_placed_uuid(reread_source) == source_placed_uuid);
  CHECK(patchy::smart_object_placed_uuid(reread_clone) == clone_placed_uuid);
  CHECK(patchy::smart_object_source_uuid(reread_clone) == source_object_uuid);
  CHECK(patchy::smart_object_source_uuid(reread_source) == source_object_uuid);
  check_pixel_layer_storage_equal(source_snapshot, reread_clone);

  // The dirty cloned SoLd changes its per-instance `placed` field, but an
  // unsupported native stack remains otherwise byte-preserved and locked.
  CHECK(filter_fx_bytes(reread_source) == original_filter_fx);
  CHECK(filter_fx_bytes(reread_clone) == original_filter_fx);
  const auto& reread_stack = require_smart_filter_stack(reread, clone_name);
  CHECK(reread_stack.support == patchy::SmartFilterStackSupport::Unsupported);
  CHECK(reread_stack.entries.size() == 2U);
  CHECK(reread_stack.entries[0].native_class_id == "GsnB");
  CHECK(reread_stack.entries[0].kind ==
        patchy::SmartFilterKind::GaussianBlur);
  CHECK(reread_stack.entries[1].native_class_id == "ZZZZ");
  CHECK(reread_stack.entries[1].kind ==
        patchy::SmartFilterKind::Unsupported);
  CHECK(patchy::smart_object_lock_reason(reread_clone) == "filters");

  const auto* source_record =
      reread.metadata().smart_filter_effects.find_unique(source_placed_uuid);
  const auto* clone_record =
      reread.metadata().smart_filter_effects.find_unique(clone_placed_uuid);
  CHECK(source_record != nullptr && clone_record != nullptr);
  CHECK(source_record != clone_record);
  CHECK(source_record->association_unique && clone_record->association_unique);
  CHECK(source_record->semantic_supported() && clone_record->semantic_supported());
  CHECK(source_record->placed_uuid == source_placed_uuid);
  CHECK(clone_record->placed_uuid == clone_placed_uuid);
}

void smart_filter_effects_store_clone_remove_and_document_snapshots_are_independent() {
  const auto fixture_path =
      patchy::test::committed_psd_fixture_path("photoshop-smart-filter-instances-base.psd");
  auto document = patchy::psd::DocumentIo::read_file(fixture_path);
  const auto& instance_a = require_layer_named(document, "Instance A Gaussian 1.5");
  const auto source_uuid = patchy::smart_object_placed_uuid(instance_a);
  const auto* source_record = document.metadata().smart_filter_effects.find_unique(source_uuid);
  CHECK(source_record != nullptr && source_record->mask.has_value());
  const auto source_raw_storage = source_record->raw_storage;
  const auto source_mask_samples = source_record->mask->samples;
  const auto original_payload = *document.metadata().smart_filter_effects.blocks.front().original_payload;

  patchy::Document snapshot = document;
  const auto* snapshot_record = snapshot.metadata().smart_filter_effects.find_unique(source_uuid);
  CHECK(snapshot_record != nullptr);
  CHECK(snapshot_record->raw_storage == source_record->raw_storage);
  CHECK(snapshot_record->mask->samples == source_record->mask->samples);
  CHECK(snapshot.metadata().smart_filter_effects.blocks.front().original_payload ==
        document.metadata().smart_filter_effects.blocks.front().original_payload);

  const auto cloned_uuid = std::string("01234567-89ab-cdef-8123-456789abcdef");
  CHECK(document.metadata().smart_filter_effects.clone_rekey(source_uuid, cloned_uuid));
  CHECK(!document.metadata().smart_filter_effects.clone_rekey(source_uuid, cloned_uuid));
  const auto* cloned = document.metadata().smart_filter_effects.find_unique(cloned_uuid);
  CHECK(cloned != nullptr);
  CHECK(cloned->raw_storage == source_raw_storage);
  CHECK(cloned->mask->samples == source_mask_samples);
  CHECK(snapshot.metadata().smart_filter_effects.find_unique(cloned_uuid) == nullptr);
  const auto rebuilt_payload =
      patchy::psd::serialize_filter_effects_block(document.metadata().smart_filter_effects.blocks.front());
  const auto rebuilt = patchy::psd::parse_filter_effects_block(
      std::string_view("FEid"), std::span<const std::uint8_t>(rebuilt_payload));
  const auto rebuilt_source = std::find_if(
      rebuilt.records.begin(), rebuilt.records.end(), [&](const patchy::SmartFilterEffectsRecord& record) {
        return record.placed_uuid == source_uuid;
      });
  const auto rebuilt_clone = std::find_if(
      rebuilt.records.begin(), rebuilt.records.end(), [&](const patchy::SmartFilterEffectsRecord& record) {
        return record.placed_uuid == cloned_uuid;
      });
  CHECK(rebuilt_source != rebuilt.records.end() && rebuilt_clone != rebuilt.records.end());
  CHECK(rebuilt_clone == rebuilt_source + 1);
  const auto source_body = patchy::psd::raw_filter_effects_record_body(*rebuilt_source);
  const auto clone_body = patchy::psd::raw_filter_effects_record_body(*rebuilt_clone);
  CHECK(source_body.size() == clone_body.size());
  CHECK(std::equal(source_body.begin() + 1 + source_body.front(), source_body.end(),
                   clone_body.begin() + 1 + clone_body.front()));
  CHECK(document.metadata().smart_filter_effects.remove(cloned_uuid));
  CHECK(patchy::psd::serialize_filter_effects_block(
            document.metadata().smart_filter_effects.blocks.front()) == original_payload);

  // Internal Copy/Paste carries a record by value and reaches adopt(), even
  // when the destination is the source document. Keep the Photoshop-required
  // source/clone adjacency rather than appending after unrelated instances.
  auto adopted_store = snapshot.metadata().smart_filter_effects;
  const auto* adopt_source = adopted_store.find_unique(source_uuid);
  CHECK(adopt_source != nullptr);
  const auto adopt_source_copy = *adopt_source;
  const auto adopted_uuid = std::string("11234567-89ab-cdef-8123-456789abcdef");
  CHECK(adopted_store.adopt(adopt_source_copy, adopted_uuid));
  const auto& adopted_records = adopted_store.blocks.front().records;
  const auto adopted_source_it = std::find_if(
      adopted_records.begin(), adopted_records.end(),
      [&](const patchy::SmartFilterEffectsRecord& record) {
        return record.placed_uuid == source_uuid;
      });
  const auto adopted_it = std::find_if(
      adopted_records.begin(), adopted_records.end(),
      [&](const patchy::SmartFilterEffectsRecord& record) {
        return record.placed_uuid == adopted_uuid;
      });
  CHECK(adopted_source_it != adopted_records.end());
  CHECK(adopted_it == adopted_source_it + 1);
  CHECK(adopted_it->raw_storage == adopt_source_copy.raw_storage);
  CHECK(adopted_it->raw_body_offset == adopt_source_copy.raw_body_offset);
  CHECK(adopted_it->raw_body_length == adopt_source_copy.raw_body_length);

  const auto instance_a_id = instance_a.id();
  CHECK(document.remove_layer(instance_a_id));
  CHECK(document.find_layer(instance_a_id) == nullptr);
  CHECK(document.metadata().smart_filter_effects.find_unique(source_uuid) == nullptr);
  CHECK(snapshot.find_layer(instance_a_id) != nullptr);
  CHECK(snapshot.metadata().smart_filter_effects.find_unique(source_uuid) != nullptr);
  document = snapshot;
  CHECK(document.find_layer(instance_a_id) != nullptr);
  CHECK(document.metadata().smart_filter_effects.find_unique(source_uuid) != nullptr);

  auto rasterized = snapshot;
  auto* rasterized_a = rasterized.find_layer(instance_a_id);
  CHECK(rasterized_a != nullptr);
  patchy::strip_layer_smart_object_data(rasterized, *rasterized_a);
  CHECK(!patchy::layer_is_smart_object(*rasterized_a));
  CHECK(rasterized_a->smart_filter_stack() == nullptr);
  CHECK(rasterized.metadata().smart_filter_effects.find_unique(source_uuid) == nullptr);
  CHECK(snapshot.metadata().smart_filter_effects.find_unique(source_uuid) != nullptr);

  // Exercise the same per-instance clone shape as Duplicate Layer and leave a
  // Patchy-written PSD for Photoshop's COM open/resave acceptance check.
  auto clone_document = patchy::psd::DocumentIo::read_file(fixture_path);
  const auto& clone_source = require_layer_named(clone_document, "Instance A Gaussian 1.5");
  const auto clone_source_uuid = patchy::smart_object_placed_uuid(clone_source);
  auto clone_layer = clone_source.clone_with_id(clone_document.allocate_layer_id());
  clone_layer.set_name("Instance A Gaussian 1.5 copy");
  patchy::set_photoshop_layer_id(
      clone_layer,
      patchy::next_photoshop_layer_id(clone_document.layers()));
  CHECK(clone_document.metadata().smart_filter_effects.clone_rekey(clone_source_uuid, cloned_uuid));
  clone_layer.metadata()[patchy::kLayerMetadataSmartObjectPlaced] = cloned_uuid;
  patchy::mark_layer_smart_object_block_dirty(clone_layer);
  clone_document.add_layer(std::move(clone_layer));
  std::filesystem::create_directories("test-artifacts");
  const auto clone_com_path =
      std::filesystem::path("test-artifacts") / "patchy-smart-filter-clone-com.psd";
  patchy::psd::DocumentIo::write_layered_rgb8_file(clone_document, clone_com_path);
  CHECK(std::filesystem::exists(clone_com_path));
  const auto clone_reread = patchy::psd::DocumentIo::read_file(clone_com_path);
  const auto& cloned_layer = require_layer_named(clone_reread, "Instance A Gaussian 1.5 copy");
  CHECK(patchy::smart_object_source_uuid(cloned_layer) ==
        patchy::smart_object_source_uuid(require_layer_named(clone_reread, "Instance A Gaussian 1.5")));
  CHECK(patchy::smart_object_placed_uuid(cloned_layer) == cloned_uuid);
  CHECK(clone_reread.metadata().smart_filter_effects.find_unique(clone_source_uuid) != nullptr);
  CHECK(clone_reread.metadata().smart_filter_effects.find_unique(cloned_uuid) != nullptr);
  CHECK(require_smart_filter_stack(clone_reread, cloned_layer.name()).support ==
        patchy::SmartFilterStackSupport::Supported);
}

}  // namespace

std::vector<patchy::test::TestCase> smart_filter_descriptors_tests() {
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
      {"psd_photoshop_high_pass_smart_filter_fixture_round_trips_and_edits",
       psd_photoshop_high_pass_smart_filter_fixture_round_trips_and_edits},
      {"psd_photoshop_median_smart_filter_fixture_round_trips_and_edits",
       psd_photoshop_median_smart_filter_fixture_round_trips_and_edits},
      {"psd_photoshop_dust_and_scratches_smart_filter_fixture_round_trips_and_edits",
       psd_photoshop_dust_and_scratches_smart_filter_fixture_round_trips_and_edits},
      {"psd_photoshop_surface_blur_smart_filter_fixture_round_trips_and_edits",
       psd_photoshop_surface_blur_smart_filter_fixture_round_trips_and_edits},
      {"psd_photoshop_plastic_wrap_smart_filter_fixture_round_trips_and_edits",
       psd_photoshop_plastic_wrap_smart_filter_fixture_round_trips_and_edits},
      {"psd_photoshop_mosaic_smart_filter_fixture_round_trips_and_edits",
       psd_photoshop_mosaic_smart_filter_fixture_round_trips_and_edits},
      {"psd_photoshop_emboss_smart_filter_fixture_round_trips_and_edits",
       psd_photoshop_emboss_smart_filter_fixture_round_trips_and_edits},
      {"psd_photoshop_box_blur_smart_filter_fixture_round_trips_and_edits",
       psd_photoshop_box_blur_smart_filter_fixture_round_trips_and_edits},
      {"psd_photoshop_unsharp_motion_smart_filter_fixture_round_trips_and_"
       "edits",
       psd_photoshop_unsharp_motion_smart_filter_fixture_round_trips_and_edits},
      {"psd_photoshop_tilt_shift_smart_filter_fixture_is_preserved_and_preview_"
       "locked",
       psd_photoshop_tilt_shift_smart_filter_fixture_is_preserved_and_preview_locked},
      {"psd_smart_filter_descriptor_semantics_parse_if_available",
       psd_smart_filter_descriptor_semantics_parse_if_available},
      {"psd_smart_filter_masks_decode_and_coexist_with_layer_mask",
       psd_smart_filter_masks_decode_and_coexist_with_layer_mask},
      {"psd_smart_filter_instances_have_independent_native_records",
       psd_smart_filter_instances_have_independent_native_records},
      {"psd_smart_filter_clean_resave_preserves_native_data_and_pixels",
       psd_smart_filter_clean_resave_preserves_native_data_and_pixels},
      {"smart_filter_effects_codec_rejects_malformed_data_and_accepts_alignment",
       smart_filter_effects_codec_rejects_malformed_data_and_accepts_alignment},
      {"smart_filter_effects_associations_fail_closed_and_preserve_raw_payload",
       smart_filter_effects_associations_fail_closed_and_preserve_raw_payload},
      {"smart_filter_unsupported_clone_preserves_filterfx_and_rekeys_cache",
       smart_filter_unsupported_clone_preserves_filterfx_and_rekeys_cache},
      {"smart_filter_effects_store_clone_remove_and_document_snapshots_are_independent",
       smart_filter_effects_store_clone_remove_and_document_snapshots_are_independent},
  };
}
