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

using patchy::test::solid_rgb;
using patchy::test::solid_rgba;

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

void document_channels_validate_crud_revisions_and_cow() {
  patchy::Document document(3, 2, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer alpha_pixels(3, 2, patchy::PixelFormat::gray8());
  for (std::int32_t y = 0; y < alpha_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < alpha_pixels.width(); ++x) {
      *alpha_pixels.pixel(x, y) = static_cast<std::uint8_t>(10 + y * 3 + x);
    }
  }

  const auto alpha_id = document.allocate_channel_id();
  auto& alpha = document.add_channel(
      patchy::DocumentChannel(alpha_id, "Alpha 1", patchy::DocumentChannelKind::Alpha, std::move(alpha_pixels)));
  CHECK(document.channels().size() == 1);
  CHECK(document.find_channel(alpha_id) == &alpha);
  CHECK(document.next_alpha_channel_name() == "Alpha 2");

  const auto revision_before_copy = std::as_const(alpha).content_revision();
  const auto* shared_pixels = std::as_const(alpha).pixels().data().data();
  patchy::Document snapshot = document;
  CHECK(std::as_const(*snapshot.find_channel(alpha_id)).pixels().data().data() == shared_pixels);
  CHECK(std::as_const(*snapshot.find_channel(alpha_id)).content_revision() == revision_before_copy);

  *document.find_channel(alpha_id)->pixels().pixel(0, 0) = 240;
  CHECK(std::as_const(*document.find_channel(alpha_id)).content_revision() != revision_before_copy);
  CHECK(std::as_const(*document.find_channel(alpha_id)).pixels().data().data() != shared_pixels);
  CHECK(*std::as_const(*snapshot.find_channel(alpha_id)).pixels().pixel(0, 0) == 10);

  patchy::PixelBuffer spot_pixels(3, 2, patchy::PixelFormat::gray8());
  spot_pixels.clear(255);
  const auto spot_id = document.allocate_channel_id();
  auto& spot = document.add_channel(
      patchy::DocumentChannel(spot_id, "Spot", patchy::DocumentChannelKind::Spot, std::move(spot_pixels)));
  CHECK(spot.display_info().color_indicates == patchy::DocumentChannelColorIndicates::SpotColor);
  CHECK(document.rename_channel(alpha_id, "Duplicate Name"));
  CHECK(document.rename_channel(spot_id, "Duplicate Name"));
  CHECK(document.reorder_channel(spot_id, 0));
  CHECK(document.channels().front().id() == spot_id);
  CHECK(document.remove_channel(alpha_id));
  CHECK(document.find_channel(alpha_id) == nullptr);

  bool rejected_wrong_size = false;
  try {
    patchy::PixelBuffer wrong_size(1, 1, patchy::PixelFormat::gray8());
    document.add_channel(patchy::DocumentChannel(document.allocate_channel_id(), "Wrong size",
                                                  patchy::DocumentChannelKind::Alpha, std::move(wrong_size)));
  } catch (const std::invalid_argument&) {
    rejected_wrong_size = true;
  }
  CHECK(rejected_wrong_size);

  bool rejected_wrong_format = false;
  try {
    patchy::PixelBuffer wrong_format(3, 2, patchy::PixelFormat::rgb8());
    (void)patchy::DocumentChannel(999, "Wrong format", patchy::DocumentChannelKind::Alpha,
                                 std::move(wrong_format));
  } catch (const std::invalid_argument&) {
    rejected_wrong_format = true;
  }
  CHECK(rejected_wrong_format);

  patchy::Document capacity_document(1, 1, patchy::PixelFormat::rgb8());
  const auto capacity = capacity_document.maximum_saved_channel_count();
  for (std::size_t index = 0; index < capacity; ++index) {
    patchy::PixelBuffer one_pixel(1, 1, patchy::PixelFormat::gray8());
    capacity_document.add_channel(patchy::DocumentChannel(
        capacity_document.allocate_channel_id(), "Capacity", patchy::DocumentChannelKind::Alpha,
        std::move(one_pixel)));
  }
  CHECK(capacity_document.channels().size() == capacity);
  bool rejected_over_capacity = false;
  try {
    patchy::PixelBuffer one_too_many(1, 1, patchy::PixelFormat::gray8());
    capacity_document.add_channel(patchy::DocumentChannel(
        capacity_document.allocate_channel_id(), "Too many", patchy::DocumentChannelKind::Alpha,
        std::move(one_too_many)));
  } catch (const std::length_error&) {
    rejected_over_capacity = true;
  }
  CHECK(rejected_over_capacity);
}

patchy::Document document_with_test_channels(std::int32_t width, std::int32_t height) {
  patchy::Document document(width, height, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Pixels", solid_rgb(width, height, 10, 20, 30));

  patchy::PixelBuffer alpha(width, height, patchy::PixelFormat::gray8());
  patchy::PixelBuffer spot(width, height, patchy::PixelFormat::gray8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      *alpha.pixel(x, y) = static_cast<std::uint8_t>(1 + y * width + x);
      *spot.pixel(x, y) = static_cast<std::uint8_t>(200 + y * width + x);
    }
  }
  document.add_channel(patchy::DocumentChannel(document.allocate_channel_id(), "Alpha",
                                                patchy::DocumentChannelKind::Alpha, std::move(alpha)));
  document.add_channel(patchy::DocumentChannel(document.allocate_channel_id(), "Spot",
                                                patchy::DocumentChannelKind::Spot, std::move(spot)));
  return document;
}

void document_channel_geometry_tracks_image_and_canvas_operations() {
  {
    auto document = document_with_test_channels(2, 2);
    patchy::resize_image_and_layers(document, 4, 4);
    CHECK(std::as_const(document).channels()[0].pixels().width() == 4);
    CHECK(std::as_const(document).channels()[0].pixels().height() == 4);
    CHECK(*std::as_const(document.channels()[0]).pixels().pixel(0, 0) == 1);
    CHECK(*std::as_const(document.channels()[0]).pixels().pixel(3, 3) == 4);
  }

  {
    auto document = document_with_test_channels(2, 2);
    patchy::resize_canvas_and_layers(document, 3, 3, patchy::CanvasAnchor::TopLeft);
    CHECK(*std::as_const(document.channels()[0]).pixels().pixel(2, 2) == 0);
    CHECK(*std::as_const(document.channels()[1]).pixels().pixel(2, 2) == 255);
  }

  {
    auto document = document_with_test_channels(3, 3);
    CHECK(patchy::crop_document(document, patchy::Rect{1, 1, 2, 2}));
    CHECK(document.width() == 2);
    CHECK(document.height() == 2);
    CHECK(*std::as_const(document.channels()[0]).pixels().pixel(0, 0) == 5);
    CHECK(*std::as_const(document.channels()[0]).pixels().pixel(1, 1) == 9);
  }

  {
    auto document = document_with_test_channels(2, 3);
    const auto original_alpha = std::as_const(document.channels()[0]).pixels().data();
    const std::vector<std::uint8_t> original_bytes(original_alpha.begin(), original_alpha.end());
    patchy::rotate_document_clockwise(document);
    CHECK(document.width() == 3);
    CHECK(document.height() == 2);
    patchy::rotate_document_counterclockwise(document);
    CHECK(document.width() == 2);
    CHECK(document.height() == 3);
    const auto restored = std::as_const(document.channels()[0]).pixels().data();
    CHECK(std::equal(restored.begin(), restored.end(), original_bytes.begin(), original_bytes.end()));
  }
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

}  // namespace

std::vector<patchy::test::TestCase> document_model_tests() {
  return {
      {"pixel_buffer_tracks_shape_and_rows", pixel_buffer_tracks_shape_and_rows},
      {"pixel_buffer_copy_shares_storage_until_mutated", pixel_buffer_copy_shares_storage_until_mutated},
      {"document_snapshot_shares_pixels_when_only_moving_a_layer",
       document_snapshot_shares_pixels_when_only_moving_a_layer},
      {"document_channels_validate_crud_revisions_and_cow",
       document_channels_validate_crud_revisions_and_cow},
      {"document_channel_geometry_tracks_image_and_canvas_operations",
       document_channel_geometry_tracks_image_and_canvas_operations},
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
  };
}
