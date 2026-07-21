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

const patchy::RadialBlurSmartFilter &
require_radial_blur_filter(const patchy::SmartFilterEntry &entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::RadialBlur);
  const auto *radial =
      std::get_if<patchy::RadialBlurSmartFilter>(&entry.parameters);
  CHECK(radial != nullptr);
  return *radial;
}

const patchy::AddNoiseSmartFilter &
require_add_noise_filter(const patchy::SmartFilterEntry &entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::AddNoise);
  const auto *noise =
      std::get_if<patchy::AddNoiseSmartFilter>(&entry.parameters);
  CHECK(noise != nullptr);
  return *noise;
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

void psd_photoshop_radial_blur_smart_filter_fixtures_round_trip_and_gate() {
  // The Spin capture imports as a fully supported, editable stack.
  const auto spin_path = patchy::test::committed_psd_fixture_path(
      "photoshop-smart-filter-radial-blur.psd");
  const auto spin = patchy::psd::DocumentIo::read_file(spin_path);
  const patchy::Layer *spin_layer = nullptr;
  for (const auto &layer : spin.layers()) {
    if (layer.smart_filter_stack() != nullptr) {
      spin_layer = &layer;
    }
  }
  CHECK(spin_layer != nullptr);
  CHECK(patchy::smart_object_lock_reason(*spin_layer).empty());
  const auto *spin_stack = spin_layer->smart_filter_stack();
  CHECK(spin_stack != nullptr &&
        spin_stack->support == patchy::SmartFilterStackSupport::Supported);
  CHECK(spin_stack->entries.size() == 1U);
  const auto &spin_entry = spin_stack->entries.front();
  CHECK(spin_entry.native_name == "Radial Blur...");
  CHECK(spin_entry.native_class_id == "RdlB");
  CHECK(spin_entry.native_filter_id == 0x52646c42U);
  const auto &spin_settings = require_radial_blur_filter(spin_entry);
  CHECK(spin_settings.amount == 10);
  CHECK(spin_settings.quality == patchy::RadialBlurQuality::Good);

  // Unedited resave keeps the SoLd byte-identical; an edit patches in place.
  const auto original_sold = require_placed_layer_block(*spin_layer).payload;
  const auto clean = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(spin));
  const auto &clean_layer = require_layer_named(clean, spin_layer->name());
  CHECK(require_placed_layer_block(clean_layer).payload == original_sold);

  auto edited = spin;
  auto *edited_layer = edited.find_layer(spin_layer->id());
  CHECK(edited_layer != nullptr);
  auto edited_stack = *edited_layer->smart_filter_stack();
  auto &edited_settings = std::get<patchy::RadialBlurSmartFilter>(
      edited_stack.entries.front().parameters);
  edited_settings.amount = 42;
  edited_settings.quality = patchy::RadialBlurQuality::Best;
  edited_layer->set_smart_filter_stack(std::move(edited_stack));
  patchy::mark_layer_smart_object_block_dirty(*edited_layer);
  const auto edited_reread = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(edited));
  const auto &reread_settings = require_radial_blur_filter(
      require_smart_filter_stack(edited_reread, spin_layer->name())
          .entries.front());
  CHECK(reread_settings.amount == 42);
  CHECK(reread_settings.quality == patchy::RadialBlurQuality::Best);

  // The Zoom capture stays fail-closed: Patchy models the Spin method only,
  // so the whole stack keeps Photoshop's preserved preview.
  const auto zoom_path = patchy::test::committed_psd_fixture_path(
      "photoshop-smart-filter-radial-blur-zoom.psd");
  const auto zoom = patchy::psd::DocumentIo::read_file(zoom_path);
  const patchy::Layer *zoom_layer = nullptr;
  for (const auto &layer : zoom.layers()) {
    if (patchy::layer_is_smart_object(layer)) {
      zoom_layer = &layer;
    }
  }
  CHECK(zoom_layer != nullptr);
  CHECK(!patchy::smart_object_lock_reason(*zoom_layer).empty());
  const auto *zoom_stack = zoom_layer->smart_filter_stack();
  CHECK(zoom_stack == nullptr ||
        zoom_stack->support == patchy::SmartFilterStackSupport::Unsupported);
  // Preservation: the zoom SoLd re-emits byte-for-byte.
  const auto zoom_sold = require_placed_layer_block(*zoom_layer).payload;
  const auto zoom_clean = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(zoom));
  CHECK(require_placed_layer_block(
            require_layer_named(zoom_clean, zoom_layer->name()))
            .payload == zoom_sold);
}

void psd_photoshop_add_noise_smart_filter_fixtures_round_trip_and_edit() {
  const auto uniform_path = patchy::test::committed_psd_fixture_path(
      "photoshop-smart-filter-add-noise.psd");
  const auto uniform = patchy::psd::DocumentIo::read_file(uniform_path);
  const patchy::Layer *uniform_layer = nullptr;
  for (const auto &layer : uniform.layers()) {
    if (layer.smart_filter_stack() != nullptr) {
      uniform_layer = &layer;
    }
  }
  CHECK(uniform_layer != nullptr);
  CHECK(patchy::smart_object_lock_reason(*uniform_layer).empty());
  const auto *uniform_stack = uniform_layer->smart_filter_stack();
  CHECK(uniform_stack != nullptr &&
        uniform_stack->support == patchy::SmartFilterStackSupport::Supported);
  const auto &uniform_entry = uniform_stack->entries.front();
  CHECK(uniform_entry.native_name == "Add Noise...");
  CHECK(uniform_entry.native_class_id == "AdNs");
  CHECK(uniform_entry.native_filter_id == 0x41644e73U);
  const auto &uniform_settings = require_add_noise_filter(uniform_entry);
  CHECK(std::abs(uniform_settings.amount_percent - 12.5) < 1e-9);
  CHECK(!uniform_settings.gaussian);
  CHECK(!uniform_settings.monochromatic);
  CHECK(uniform_settings.seed == 123456);

  const auto gaussian_path = patchy::test::committed_psd_fixture_path(
      "photoshop-smart-filter-add-noise-gaussian.psd");
  const auto gaussian = patchy::psd::DocumentIo::read_file(gaussian_path);
  const patchy::Layer *gaussian_layer = nullptr;
  for (const auto &layer : gaussian.layers()) {
    if (layer.smart_filter_stack() != nullptr) {
      gaussian_layer = &layer;
    }
  }
  CHECK(gaussian_layer != nullptr);
  const auto &gaussian_settings = require_add_noise_filter(
      require_smart_filter_stack(gaussian, gaussian_layer->name())
          .entries.front());
  CHECK(std::abs(gaussian_settings.amount_percent - 8.0) < 1e-9);
  CHECK(gaussian_settings.gaussian);
  CHECK(gaussian_settings.monochromatic);
  CHECK(gaussian_settings.seed == 654321);

  // Unedited resave keeps the SoLd byte-identical; an edit patches known
  // leaves and preserves the imported seed.
  const auto original_sold = require_placed_layer_block(*uniform_layer).payload;
  const auto clean = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(uniform));
  CHECK(require_placed_layer_block(
            require_layer_named(clean, uniform_layer->name()))
            .payload == original_sold);

  auto edited = uniform;
  auto *edited_layer = edited.find_layer(uniform_layer->id());
  CHECK(edited_layer != nullptr);
  auto edited_stack = *edited_layer->smart_filter_stack();
  auto &edited_settings = std::get<patchy::AddNoiseSmartFilter>(
      edited_stack.entries.front().parameters);
  edited_settings.amount_percent = 33.25;
  edited_settings.gaussian = true;
  edited_settings.monochromatic = true;
  edited_layer->set_smart_filter_stack(std::move(edited_stack));
  patchy::mark_layer_smart_object_block_dirty(*edited_layer);
  const auto edited_reread = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(edited));
  const auto &reread_settings = require_add_noise_filter(
      require_smart_filter_stack(edited_reread, uniform_layer->name())
          .entries.front());
  CHECK(std::abs(reread_settings.amount_percent - 33.25) < 1e-9);
  CHECK(reread_settings.gaussian);
  CHECK(reread_settings.monochromatic);
  CHECK(reread_settings.seed == 123456);
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

std::vector<patchy::test::TestCase> smart_filter_descriptors_tests_part2() {
  return {
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
      {"psd_photoshop_radial_blur_smart_filter_fixtures_round_trip_and_gate",
       psd_photoshop_radial_blur_smart_filter_fixtures_round_trip_and_gate},
      {"psd_photoshop_add_noise_smart_filter_fixtures_round_trip_and_edit",
       psd_photoshop_add_noise_smart_filter_fixtures_round_trip_and_edit},
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
