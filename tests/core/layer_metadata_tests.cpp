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

using patchy::test::read_u32_be_at;
using patchy::test::solid_rgba;

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
  const auto initial_pixel_revision = layer.pixel_revision();

  layer.raw_psd_blending_ranges() = {0, 12, 240, 255, 3, 18, 220, 251};
  layer.raw_psd_group_boundary_blending_ranges() = {5, 20, 210, 245, 9, 24, 200, 239};
  CHECK(layer.content_revision() == initial_content_revision);
  CHECK(layer.render_revision() == initial_render_revision);
  CHECK(layer.pixel_revision() == initial_pixel_revision);

  layer.set_bounds(patchy::Rect{10, 12, 4, 4});
  CHECK(layer.content_revision() == initial_content_revision);
  CHECK(layer.render_revision() > initial_render_revision);
  CHECK(layer.pixel_revision() == initial_pixel_revision);

  const auto after_move_content_revision = layer.content_revision();
  layer.set_name("Renamed Paint");
  CHECK(layer.content_revision() == after_move_content_revision);

  layer.set_opacity(0.5F);
  CHECK(layer.content_revision() > after_move_content_revision);
  CHECK(layer.pixel_revision() == initial_pixel_revision);

  const auto after_opacity_content_revision = layer.content_revision();
  layer.set_fill_opacity(0.37F);
  CHECK(layer.content_revision() > after_opacity_content_revision);
  CHECK(layer.pixel_revision() == initial_pixel_revision);

  const auto after_fill_opacity_content_revision = layer.content_revision();
  layer.set_blend_mode(patchy::BlendMode::Multiply);
  CHECK(layer.content_revision() > after_fill_opacity_content_revision);

  const auto after_blend_content_revision = layer.content_revision();
  auto* px = layer.pixels().pixel(0, 0);
  px[0] = 120;
  CHECK(layer.content_revision() > after_blend_content_revision);
  CHECK(layer.pixel_revision() > initial_pixel_revision);

  const auto after_pixels_content_revision = layer.content_revision();
  const auto after_pixels_pixel_revision = layer.pixel_revision();
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  layer.layer_style().strokes.push_back(stroke);
  CHECK(layer.content_revision() > after_pixels_content_revision);
  CHECK(layer.pixel_revision() == after_pixels_pixel_revision);

  const auto after_style_content_revision = layer.content_revision();
  patchy::LayerMask mask;
  mask.bounds = patchy::Rect{10, 12, 4, 4};
  mask.pixels = patchy::PixelBuffer(4, 4, patchy::PixelFormat::gray8());
  mask.pixels.clear(255);
  layer.set_mask(std::move(mask));
  CHECK(layer.content_revision() > after_style_content_revision);
  CHECK(layer.pixel_revision() == after_pixels_pixel_revision);
}

void layer_set_clipped_bumps_render_revision_only() {
  patchy::Layer layer(9, "Clip", solid_rgba(2, 2, 10, 20, 30, 255));
  CHECK(!layer.clipped());
  const auto content_before = layer.content_revision();
  const auto render_before = layer.render_revision();

  layer.set_clipped(true);
  CHECK(layer.clipped());
  CHECK(layer.render_revision() > render_before);
  CHECK(layer.content_revision() == content_before);

  const auto render_after_set = layer.render_revision();
  layer.mark_render_changed();
  CHECK(layer.render_revision() > render_after_set);
  CHECK(layer.content_revision() == content_before);

  layer.unknown_psd_blocks().push_back(
      patchy::UnknownPsdBlock{"lyid", {0, 0, 0, 7}});
  layer.unknown_psd_blocks().push_back(
      patchy::UnknownPsdBlock{"test", {1, 2, 3, 4}});
  const auto clone = layer.clone_with_id(42);
  CHECK(clone.clipped());
  CHECK(clone.id() == 42);
  CHECK(patchy::photoshop_layer_id(clone) == 7U);
  CHECK(std::any_of(clone.unknown_psd_blocks().begin(),
                    clone.unknown_psd_blocks().end(),
                    [](const patchy::UnknownPsdBlock& block) {
                      return block.key == "test";
                    }));
  auto rekeyed_clone = clone;
  patchy::set_photoshop_layer_id(rekeyed_clone, 9U);
  CHECK(patchy::photoshop_layer_id(rekeyed_clone) == 9U);
  CHECK(patchy::photoshop_layer_id(clone) == 7U);
  CHECK(patchy::next_photoshop_layer_id(
            std::vector<patchy::Layer>{layer, rekeyed_clone}) == 10U);

  auto max_id = clone.clone_with_id(45);
  patchy::set_photoshop_layer_id(
      max_id, std::numeric_limits<std::uint32_t>::max());
  auto id_one = clone.clone_with_id(46);
  patchy::set_photoshop_layer_id(id_one, 1U);
  auto id_three = clone.clone_with_id(47);
  patchy::set_photoshop_layer_id(id_three, 3U);
  CHECK(patchy::next_photoshop_layer_id(
            std::vector<patchy::Layer>{max_id, id_one, id_three}) == 2U);

  std::vector<patchy::Layer> siblings;
  siblings.push_back(patchy::Layer(1, "Base", solid_rgba(2, 2, 1, 2, 3, 255)));
  siblings.push_back(std::move(layer));
  siblings.push_back(clone.clone_with_id(43));
  CHECK(patchy::effective_clip_base(siblings, 1) == &siblings[0]);
  // A run member higher up walks through the clipped sibling below to the base.
  CHECK(patchy::effective_clip_base(siblings, 2) == &siblings[0]);
  CHECK(patchy::effective_clip_base(siblings, 0) == nullptr);

  std::vector<patchy::Layer> group_base;
  group_base.push_back(patchy::Layer(4, "Folder", patchy::LayerKind::Group));
  group_base.push_back(clone.clone_with_id(44));
  CHECK(patchy::effective_clip_base(group_base, 1) == nullptr);
}

std::int32_t read_i32_be_at(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::int32_t>(read_u32_be_at(bytes, offset));
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

}  // namespace

std::vector<patchy::test::TestCase> layer_metadata_tests() {
  return {
      {"layer_affine_transform_metadata_parses_serializes_and_composes",
       layer_affine_transform_metadata_parses_serializes_and_composes},
      {"layer_lock_flags_and_inheritance_work", layer_lock_flags_and_inheritance_work},
      {"moved_layer_metadata_translates_linked_masks_and_vector_paths",
       moved_layer_metadata_translates_linked_masks_and_vector_paths},
      {"moved_layer_metadata_leaves_unlinked_masks_stationary",
       moved_layer_metadata_leaves_unlinked_masks_stationary},
      {"layer_content_revision_ignores_translation_and_tracks_render_content",
       layer_content_revision_ignores_translation_and_tracks_render_content},
      {"layer_set_clipped_bumps_render_revision_only", layer_set_clipped_bumps_render_revision_only},
  };
}
