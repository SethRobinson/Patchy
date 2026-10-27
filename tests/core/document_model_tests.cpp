#include <string_view>
#include "core/adjustment_layer.hpp"
#include "core/channel_view.hpp"
#include "core/document.hpp"
#include "core/document_memory.hpp"
#include "core/stroke_patch.hpp"
#include "core/layer_render_utils.hpp"
#include "core/pixel_depth.hpp"
#include "core/document_depth.hpp"
#include "formats/document_flatten.hpp"
#include "core/layer_metadata.hpp"
#include "render/compositor.hpp"
#include "core/layer_tree.hpp"
#include "core/pixel_tools.hpp"
#include "render/compositor.hpp"
#include "test_harness.hpp"
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
#include <set>
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

// The history byte budget's accounting (core/document_memory.hpp): buffers
// shared with the live document are free, storage shared between snapshots
// counts once, empty buffers never count, and the const walk bumps nothing.
void document_memory_accounting_is_cow_aware() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgba8());
  const auto layer_id = document.add_pixel_layer("Paint", solid_rgba(64, 48, 10, 20, 30, 255)).id();
  const auto masked_id = document.add_pixel_layer("Masked", solid_rgba(64, 48, 1, 2, 3, 255)).id();
  patchy::LayerMask mask;
  mask.bounds = patchy::Rect{0, 0, 64, 48};
  mask.pixels = patchy::PixelBuffer(64, 48, patchy::PixelFormat::gray8());
  document.find_layer(masked_id)->set_mask(std::move(mask));

  // A fresh snapshot shares every buffer with the live document: zero marginal bytes.
  patchy::Document snapshot = document;
  patchy::PixelStorageSet live;
  patchy::collect_pixel_storage(document, live);
  patchy::PixelStorageSet seen;
  CHECK(patchy::accumulate_unique_pixel_bytes(snapshot, live, seen) == 0);

  // The accounting walk uses const access only, so revisions never move.
  const auto pixel_revision_before = std::as_const(document).find_layer(layer_id)->pixel_revision();
  const auto content_revision_before = std::as_const(document).find_layer(layer_id)->content_revision();
  patchy::visit_pixel_buffers(document, [](const patchy::PixelBuffer&) {});
  CHECK(std::as_const(document).find_layer(layer_id)->pixel_revision() == pixel_revision_before);
  CHECK(std::as_const(document).find_layer(layer_id)->content_revision() == content_revision_before);

  // Detaching one live layer leaves the snapshot holding exactly that layer's
  // bytes as marginal cost; a second snapshot of the same storage adds nothing.
  document.find_layer(layer_id)->pixels().pixel(0, 0)[0] = 99;
  patchy::PixelStorageSet live_after;
  patchy::collect_pixel_storage(document, live_after);
  patchy::PixelStorageSet seen_after;
  const auto expected = std::as_const(snapshot).find_layer(layer_id)->pixels().byte_size();
  CHECK(expected == 64U * 48U * 4U);
  CHECK(patchy::accumulate_unique_pixel_bytes(snapshot, live_after, seen_after) == expected);
  patchy::Document second_snapshot = snapshot;
  CHECK(patchy::accumulate_unique_pixel_bytes(second_snapshot, live_after, seen_after) == 0);

  // Empty buffers all alias one sentinel storage; they are skipped, never
  // counted (add_layer, not add_pixel_layer: the latter rejects buffers that
  // do not match the document dimensions).
  patchy::Document empty_document(8, 8, patchy::PixelFormat::rgba8());
  empty_document.add_layer(
      patchy::Layer(empty_document.allocate_layer_id(), "Empty A", patchy::PixelBuffer{}));
  empty_document.add_layer(
      patchy::Layer(empty_document.allocate_layer_id(), "Empty B", patchy::PixelBuffer{}));
  patchy::PixelStorageSet no_exclusions;
  patchy::PixelStorageSet empty_seen;
  CHECK(patchy::accumulate_unique_pixel_bytes(empty_document, no_exclusions, empty_seen) == 0);
  CHECK(empty_seen.empty());
}

// Per-stroke history (core/stroke_patch.hpp): a patch holds only the stroke's
// rectangle, restores and redoes it exactly, and the capture refuses whenever
// the stroke changed anything else.
void stroke_patch_captures_and_restores_only_the_stroke_rect() {
  using patchy::StrokePatch;
  patchy::Document document(64, 48, patchy::PixelFormat::rgba8());
  const auto layer_id = document.add_pixel_layer("Paint", solid_rgba(64, 48, 10, 20, 30, 255)).id();
  const auto masked_id = document.add_pixel_layer("Masked", solid_rgba(64, 48, 1, 2, 3, 255)).id();
  patchy::LayerMask mask;
  mask.bounds = patchy::Rect{4, 2, 40, 30};
  mask.pixels = patchy::PixelBuffer(40, 30, patchy::PixelFormat::gray8());
  mask.pixels.clear(255);
  document.find_layer(masked_id)->set_mask(std::move(mask));
  const patchy::Document snapshot = document;

  // Nothing written yet: the snapshot costs nothing, so there is no patch.
  CHECK(!patchy::capture_stroke_patch(snapshot, document, StrokePatch::Target::LayerContent, layer_id, 0,
                                      patchy::Rect{8, 4, 12, 8})
             .has_value());

  // A write inside the reported rectangle becomes a patch of that rectangle.
  auto& live = document.find_layer(layer_id)->pixels();
  for (std::int32_t y = 4; y < 12; ++y) {
    for (std::int32_t x = 8; x < 20; ++x) {
      live.pixel(x, y)[0] = 200;
    }
  }
  const auto pixel_revision_before = std::as_const(document).find_layer(layer_id)->pixel_revision();
  auto patch = patchy::capture_stroke_patch(snapshot, document, StrokePatch::Target::LayerContent, layer_id, 0,
                                            patchy::Rect{6, 2, 16, 12});
  CHECK(patch.has_value());
  CHECK(patch->rect.x == 6 && patch->rect.y == 2 && patch->rect.width == 16 && patch->rect.height == 12);
  CHECK(patch->byte_size() == 16U * 12U * 4U);
  CHECK(patch->before.pixel(2, 2)[0] == 10);
  // The capture reads through const access: no revision moved.
  CHECK(std::as_const(document).find_layer(layer_id)->pixel_revision() == pixel_revision_before);

  // Restore puts the snapshot bytes back (and remembers the stroke for redo).
  const patchy::PixelBuffer after_stroke = std::as_const(document).find_layer(layer_id)->pixels();
  CHECK(patchy::apply_stroke_patch(document, *patch, /*restore_before=*/true));
  CHECK(std::as_const(document).find_layer(layer_id)->pixel_revision() != pixel_revision_before);
  CHECK(patch->byte_size() == 2U * 16U * 12U * 4U);
  {
    const auto& restored = std::as_const(document).find_layer(layer_id)->pixels();
    const auto& original = std::as_const(snapshot).find_layer(layer_id)->pixels();
    CHECK(std::equal(restored.data().begin(), restored.data().end(), original.data().begin()));
  }
  CHECK(patchy::apply_stroke_patch(document, *patch, /*restore_before=*/false));
  {
    const auto& redone = std::as_const(document).find_layer(layer_id)->pixels();
    CHECK(std::equal(redone.data().begin(), redone.data().end(), after_stroke.data().begin()));
  }

  // A second detached buffer (the mask) means the stroke changed more than its
  // target: no patch. A write outside the reported rectangle is caught the same
  // way, since the rectangle then clips to the buffer but the detach is real.
  patchy::Document two_writes = snapshot;
  two_writes.find_layer(layer_id)->pixels().pixel(8, 4)[0] = 7;
  two_writes.find_layer(masked_id)->mask()->pixels.pixel(0, 0)[0] = 0;
  CHECK(!patchy::capture_stroke_patch(snapshot, two_writes, StrokePatch::Target::LayerContent, layer_id, 0,
                                      patchy::Rect{8, 4, 4, 4})
             .has_value());
  patchy::Document moved = snapshot;
  moved.find_layer(layer_id)->pixels().pixel(8, 4)[0] = 7;
  moved.find_layer(layer_id)->set_bounds(patchy::Rect{1, 0, 64, 48});
  CHECK(!patchy::capture_stroke_patch(snapshot, moved, StrokePatch::Target::LayerContent, layer_id, 0,
                                      patchy::Rect{8, 4, 4, 4})
             .has_value());
  patchy::Document outside = snapshot;
  outside.find_layer(layer_id)->pixels().pixel(60, 40)[0] = 7;
  CHECK(!patchy::capture_stroke_patch(snapshot, outside, StrokePatch::Target::LayerContent, layer_id, 0,
                                      patchy::Rect{-10, -10, 5, 5})
             .has_value());

  // Mask target: the document rectangle maps through the mask's own origin.
  patchy::Document mask_stroke = snapshot;
  mask_stroke.find_layer(masked_id)->mask()->pixels.pixel(6, 3)[0] = 0;  // document (10, 5)
  auto mask_patch = patchy::capture_stroke_patch(snapshot, mask_stroke, StrokePatch::Target::LayerMask, masked_id, 0,
                                                 patchy::Rect{9, 4, 3, 3});
  CHECK(mask_patch.has_value());
  CHECK(mask_patch->rect.x == 5 && mask_patch->rect.y == 2 && mask_patch->rect.width == 3 && mask_patch->rect.height == 3);
  CHECK(mask_patch->document_rect.x == 9 && mask_patch->document_rect.y == 4);
  CHECK(mask_patch->before.pixel(1, 1)[0] == 255);
  CHECK(patchy::apply_stroke_patch(mask_stroke, *mask_patch, /*restore_before=*/true));
  CHECK(std::as_const(mask_stroke).find_layer(masked_id)->mask()->pixels.pixel(6, 3)[0] == 255);
  CHECK(patchy::apply_stroke_patch(mask_stroke, *mask_patch, /*restore_before=*/false));
  CHECK(std::as_const(mask_stroke).find_layer(masked_id)->mask()->pixels.pixel(6, 3)[0] == 0);
  // A missing target applies nothing.
  mask_stroke.find_layer(masked_id)->clear_mask();
  CHECK(!patchy::apply_stroke_patch(mask_stroke, *mask_patch, /*restore_before=*/true));
}

void channel_view_eyes_follow_the_composite_rules() {
  using patchy::ChannelViewGroup;
  constexpr auto color = ChannelViewGroup::Color;
  constexpr auto ink = ChannelViewGroup::Ink;
  // The composite: every eye on, the composite targeted.
  const auto start = patchy::select_composite_channel();
  CHECK(patchy::channel_view_is_composite(start.view));
  for (int index = 0; index < 3; ++index) {
    CHECK(patchy::channel_view_eye_on(start.view, color, index));
  }
  CHECK(patchy::channel_view_eye_on(start.view, ink, 3));
  // Hiding Blue keeps the composite target (painting continues) and turns the composite off.
  const auto no_blue = patchy::toggle_channel_eye(start.view, start.target, color, 2);
  CHECK(!patchy::channel_view_is_composite(no_blue.view));
  CHECK(no_blue.view.visible == 0b011);
  CHECK(no_blue.target.composite);
  CHECK(!patchy::channel_view_eye_on(no_blue.view, color, 2));
  // Hiding Green as well leaves Red alone: Red becomes the target.
  const auto red_only = patchy::toggle_channel_eye(no_blue.view, no_blue.target, color, 1);
  CHECK(red_only.view.visible == 0b001);
  CHECK(red_only.target == patchy::channel_view_target(color, 0));
  // The last visible channel stays on.
  const auto still_red = patchy::toggle_channel_eye(red_only.view, red_only.target, color, 0);
  CHECK(still_red.view == red_only.view);
  CHECK(still_red.target == red_only.target);
  // Red became the target only because it was left: showing Green again returns to the
  // composite target.
  const auto red_green = patchy::toggle_channel_eye(red_only.view, red_only.target, color, 1);
  CHECK(red_green.view.visible == 0b011);
  CHECK(red_green.target.composite);
  // Hiding Red leaves Green, which becomes the target.
  const auto green_only = patchy::toggle_channel_eye(red_green.view, red_green.target, color, 0);
  CHECK(green_only.view.visible == 0b010);
  CHECK(green_only.target == patchy::channel_view_target(color, 1));
  // Turning every channel back on returns to the composite.
  const auto two = patchy::toggle_channel_eye(green_only.view, green_only.target, color, 0);
  const auto all = patchy::toggle_channel_eye(two.view, two.target, color, 2);
  CHECK(patchy::channel_view_is_composite(all.view));
  CHECK(all.target.composite);
  // Selecting a channel row shows it alone; the composite row brings every channel back.
  const auto blue = patchy::select_single_channel(color, 2);
  CHECK(blue.view.visible == 0b100);
  CHECK(blue.target == patchy::channel_view_target(color, 2));
  CHECK(patchy::select_composite_channel().view == patchy::composite_channel_view());
  // Ink plates follow the same rules in their own group; the first plate eye clicked
  // while a color channel shows alone starts the plates from that plate alone.
  const auto cyan = patchy::toggle_channel_eye(blue.view, blue.target, ink, 0);
  CHECK(cyan.view == patchy::single_channel_view(ink, 0));
  CHECK(cyan.target == patchy::channel_view_target(ink, 0));
  CHECK(!patchy::channel_view_eye_on(cyan.view, color, 2));
  const auto no_black = patchy::toggle_channel_eye(start.view, start.target, ink, 3);
  CHECK(no_black.view.group == ink);
  CHECK(no_black.view.visible == 0b0111);
  CHECK(no_black.target == patchy::channel_view_target(ink, 0));
  const auto all_inks = patchy::toggle_channel_eye(no_black.view, no_black.target, ink, 3);
  CHECK(patchy::channel_view_is_composite(all_inks.view));
  CHECK(all_inks.view == patchy::composite_channel_view());
  // Another target (a layer mask, a saved channel) keeps a two-channel color view only.
  CHECK(patchy::channel_view_for_target(no_blue.view, patchy::composite_channel_target()) == no_blue.view);
  CHECK(patchy::channel_view_for_target(red_only.view, patchy::composite_channel_target()) ==
        patchy::composite_channel_view());
  CHECK(patchy::channel_view_for_target(no_black.view, patchy::composite_channel_target()) ==
        patchy::composite_channel_view());
  CHECK(patchy::channel_view_for_target(red_green.view, patchy::channel_view_target(color, 1)) == red_green.view);
  CHECK(patchy::channel_view_for_target(red_green.view, patchy::channel_view_target(color, 2)) ==
        patchy::single_channel_view(color, 2));

  // Seth's sequence (October 25, 2026): from the composite hide Blue, then Green. Red is the
  // one channel left and becomes the target; showing Green again returns to the composite
  // target with Red and Green showing, and showing Blue returns to the composite.
  const auto hide_blue = patchy::toggle_channel_eye(start.view, start.target, color, 2);
  const auto hide_green = patchy::toggle_channel_eye(hide_blue.view, hide_blue.target, color, 1);
  CHECK(hide_green.target == patchy::channel_view_target(color, 0));
  CHECK(hide_green.view.composite_target_on_reshow);
  const auto reshow_green = patchy::toggle_channel_eye(hide_green.view, hide_green.target, color, 1);
  CHECK(reshow_green.view.visible == 0b011);
  CHECK(reshow_green.target.composite);
  CHECK(!reshow_green.view.composite_target_on_reshow);
  const auto reshow_blue = patchy::toggle_channel_eye(reshow_green.view, reshow_green.target, color, 2);
  CHECK(reshow_blue.view == patchy::composite_channel_view());
  CHECK(reshow_blue.target.composite);
  // Showing Blue instead of Green after the two hides also returns to the composite target.
  const auto red_blue = patchy::toggle_channel_eye(hide_green.view, hide_green.target, color, 2);
  CHECK(red_blue.view.visible == 0b101);
  CHECK(red_blue.target.composite);
  // Hiding in the other order (Red, then Green) leaves Blue targeted, never a hidden channel.
  const auto hide_red = patchy::toggle_channel_eye(start.view, start.target, color, 0);
  CHECK(hide_red.target.composite);
  const auto blue_left = patchy::toggle_channel_eye(hide_red.view, hide_red.target, color, 1);
  CHECK(blue_left.target == patchy::channel_view_target(color, 2));
  // An eye click never targets the channel it hid or a hidden channel.
  for (int first = 0; first < 3; ++first) {
    for (int second = 0; second < 3; ++second) {
      const auto hide_first = patchy::toggle_channel_eye(start.view, start.target, color, first);
      const auto hide_second = patchy::toggle_channel_eye(hide_first.view, hide_first.target, color, second);
      for (const auto& step : {hide_first, hide_second}) {
        CHECK(step.target.composite || patchy::channel_view_shows(step.view, step.target.group, step.target.index));
      }
    }
  }
  // A channel picked by its row stays the target when a second channel is shown.
  const auto picked = patchy::select_single_channel(color, 1);
  const auto picked_plus = patchy::toggle_channel_eye(picked.view, picked.target, color, 2);
  CHECK(picked_plus.view.visible == 0b110);
  CHECK(picked_plus.target == patchy::channel_view_target(color, 1));
  // The other target changes drop the reshow flag of a composite-targeted color view.
  CHECK(!patchy::channel_view_for_target(reshow_green.view, patchy::composite_channel_target())
             .composite_target_on_reshow);
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

void only_layer_id_needs_exactly_one_layer_in_the_tree() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  CHECK(!patchy::only_layer_id(std::as_const(document).layers()).has_value());
  patchy::Layer pixel(document.allocate_layer_id(), "Pixel", solid_rgb(2, 2, 10, 20, 30));
  const auto pixel_id = pixel.id();
  document.add_layer(std::move(pixel));
  CHECK(patchy::only_layer_id(std::as_const(document).layers()).value() == pixel_id);
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Second", solid_rgb(2, 2, 40, 50, 60)));
  CHECK(!patchy::only_layer_id(std::as_const(document).layers()).has_value());

  // A folder holding one layer is two layers, not one.
  patchy::Document grouped(2, 2, patchy::PixelFormat::rgb8());
  patchy::Layer group(grouped.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  group.add_child(patchy::Layer(grouped.allocate_layer_id(), "Child", solid_rgb(2, 2, 10, 20, 30)));
  grouped.add_layer(std::move(group));
  CHECK(!patchy::only_layer_id(std::as_const(grouped).layers()).has_value());
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

// Pick tool Sample choices (GitHub issue 321): the layers each composite choice
// leaves out, with groups (the later siblings at each level of the active
// layer's path) and adjustment layers at any depth.
void sample_layers_excluded_ids_cover_current_and_below_and_no_adjustments() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  const auto base_id = document.add_pixel_layer("Base", solid_rgb(2, 2, 10, 20, 30)).id();
  patchy::AdjustmentSettings invert;
  invert.kind = patchy::AdjustmentKind::Invert;
  patchy::Layer adjustment_below(document.allocate_layer_id(), "Invert below", patchy::LayerKind::Adjustment);
  patchy::configure_adjustment_layer(adjustment_below, invert);
  const auto adjustment_below_id = adjustment_below.id();
  document.add_layer(std::move(adjustment_below));
  patchy::Layer group(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  const auto group_id = group.id();
  patchy::Layer child_below(document.allocate_layer_id(), "Child below", solid_rgb(2, 2, 1, 2, 3));
  patchy::Layer nested_adjustment(document.allocate_layer_id(), "Nested invert", patchy::LayerKind::Adjustment);
  patchy::configure_adjustment_layer(nested_adjustment, invert);
  patchy::Layer active(document.allocate_layer_id(), "Active", solid_rgb(2, 2, 4, 5, 6));
  patchy::Layer child_above(document.allocate_layer_id(), "Child above", solid_rgb(2, 2, 7, 8, 9));
  const auto child_below_id = child_below.id();
  const auto nested_adjustment_id = nested_adjustment.id();
  const auto active_id = active.id();
  const auto child_above_id = child_above.id();
  group.add_child(std::move(child_below));
  group.add_child(std::move(nested_adjustment));
  group.add_child(std::move(active));
  group.add_child(std::move(child_above));
  document.add_layer(std::move(group));
  patchy::Layer adjustment_above(document.allocate_layer_id(), "Invert above", patchy::LayerKind::Adjustment);
  patchy::configure_adjustment_layer(adjustment_above, invert);
  const auto adjustment_above_id = adjustment_above.id();
  document.add_layer(std::move(adjustment_above));
  const auto top_id = document.add_pixel_layer("Top", solid_rgb(2, 2, 90, 90, 90)).id();
  const auto& layers = std::as_const(document).layers();
  using patchy::SampleLayers;
  using Ids = std::set<patchy::LayerId>;
  const auto excluded = [&layers](std::optional<patchy::LayerId> active_layer, SampleLayers mode) {
    const auto ids = patchy::sample_layers_excluded_ids(layers, active_layer, mode);
    return ids.has_value() ? std::optional<Ids>(Ids(ids->begin(), ids->end())) : std::nullopt;
  };

  CHECK(!excluded(active_id, SampleLayers::CurrentLayer).has_value());
  CHECK(excluded(active_id, SampleLayers::AllLayers) == Ids{});
  CHECK(excluded(active_id, SampleLayers::AllLayersNoAdjustments) ==
        (Ids{adjustment_below_id, nested_adjustment_id, adjustment_above_id}));
  CHECK(excluded(active_id, SampleLayers::CurrentAndBelow) == (Ids{child_above_id, adjustment_above_id, top_id}));
  CHECK(excluded(active_id, SampleLayers::CurrentAndBelowNoAdjustments) ==
        (Ids{child_above_id, adjustment_above_id, top_id, adjustment_below_id, nested_adjustment_id}));
  // A selected folder samples all of it; the bottom layer samples only itself.
  CHECK(excluded(group_id, SampleLayers::CurrentAndBelow) == (Ids{adjustment_above_id, top_id}));
  CHECK(excluded(base_id, SampleLayers::CurrentAndBelow) ==
        (Ids{adjustment_below_id, group_id, adjustment_above_id, top_id}));
  CHECK(excluded(child_below_id, SampleLayers::CurrentAndBelow) ==
        (Ids{nested_adjustment_id, active_id, child_above_id, adjustment_above_id, top_id}));
  CHECK(excluded(top_id, SampleLayers::CurrentAndBelow) == Ids{});
  // Current & Below needs an active layer in the tree.
  CHECK(!excluded(std::nullopt, SampleLayers::CurrentAndBelow).has_value());
  CHECK(!excluded(patchy::LayerId{9999}, SampleLayers::CurrentAndBelowNoAdjustments).has_value());
  CHECK(excluded(std::nullopt, SampleLayers::AllLayersNoAdjustments).has_value());
}

// The composite each Sample choice reads (GitHub issue 321): an Invert below and
// above a half-opaque active layer, under a half-opaque red layer, gives a
// different color per choice once the excluded layers are hidden.
void sample_layers_composites_differ_per_choice() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(1, 1, 60, 120, 200));
  patchy::AdjustmentSettings invert;
  invert.kind = patchy::AdjustmentKind::Invert;
  patchy::Layer adjustment_below(document.allocate_layer_id(), "Invert below", patchy::LayerKind::Adjustment);
  patchy::configure_adjustment_layer(adjustment_below, invert);
  document.add_layer(std::move(adjustment_below));
  auto& active = document.add_pixel_layer("Active", solid_rgb(1, 1, 40, 160, 60));
  active.set_opacity(0.5F);
  const auto active_id = active.id();
  patchy::Layer adjustment_above(document.allocate_layer_id(), "Invert above", patchy::LayerKind::Adjustment);
  patchy::configure_adjustment_layer(adjustment_above, invert);
  document.add_layer(std::move(adjustment_above));
  document.add_pixel_layer("Top", solid_rgb(1, 1, 220, 40, 40)).set_opacity(0.5F);
  document.set_active_layer(active_id);

  const auto composite = [&document, active_id](patchy::SampleLayers mode) {
    auto copy = document;
    const auto excluded = patchy::sample_layers_excluded_ids(std::as_const(copy).layers(), active_id, mode)
                              .value_or(std::vector<patchy::LayerId>{});
    for (const auto id : excluded) {
      copy.find_layer(id)->set_visible(false);
    }
    const auto flat = patchy::Compositor{}.flatten_rgb8(copy);
    const auto* pixel = flat.pixel(0, 0);
    return std::array<int, 3>{pixel[0], pixel[1], pixel[2]};
  };
  const auto close_to = [](std::array<int, 3> actual, std::array<int, 3> expected) {
    for (std::size_t channel = 0; channel < 3; ++channel) {
      if (std::abs(actual[channel] - expected[channel]) > 2) {
        return false;
      }
    }
    return true;
  };
  using patchy::SampleLayers;
  // Base inverted (195,135,55), then half the active green.
  CHECK(close_to(composite(SampleLayers::CurrentAndBelow), {117, 147, 57}));
  // That inverted (138,108,198), then half the red on top.
  CHECK(close_to(composite(SampleLayers::AllLayers), {179, 74, 119}));
  // No inverts: the base and half the green, then half the red.
  CHECK(close_to(composite(SampleLayers::CurrentAndBelowNoAdjustments), {50, 140, 130}));
  CHECK(close_to(composite(SampleLayers::AllLayersNoAdjustments), {135, 90, 85}));
}

// The stored Sample choice (GitHub issue 321): a known token wins; otherwise the
// pre-321 bool maps (true All Layers, false Current Layer); otherwise All Layers.
void sample_layers_settings_map_tokens_and_legacy_bool() {
  using patchy::SampleLayers;
  for (const auto mode : {SampleLayers::CurrentLayer, SampleLayers::CurrentAndBelow, SampleLayers::AllLayers,
                          SampleLayers::AllLayersNoAdjustments, SampleLayers::CurrentAndBelowNoAdjustments}) {
    CHECK(patchy::sample_layers_from_settings(patchy::sample_layers_token(mode), false) == mode);
    CHECK(patchy::sample_layers_from_settings(patchy::sample_layers_token(mode), std::nullopt) == mode);
  }
  // Permanent identifiers.
  CHECK(patchy::sample_layers_token(SampleLayers::CurrentLayer) == "current");
  CHECK(patchy::sample_layers_token(SampleLayers::CurrentAndBelow) == "currentAndBelow");
  CHECK(patchy::sample_layers_token(SampleLayers::AllLayers) == "all");
  CHECK(patchy::sample_layers_token(SampleLayers::AllLayersNoAdjustments) == "allNoAdjustments");
  CHECK(patchy::sample_layers_token(SampleLayers::CurrentAndBelowNoAdjustments) == "currentAndBelowNoAdjustments");
  CHECK(patchy::sample_layers_from_settings(std::nullopt, false) == SampleLayers::CurrentLayer);
  CHECK(patchy::sample_layers_from_settings(std::nullopt, true) == SampleLayers::AllLayers);
  CHECK(patchy::sample_layers_from_settings(std::nullopt, std::nullopt) == SampleLayers::AllLayers);
  CHECK(patchy::sample_layers_from_settings(std::string_view("bogus"), false) == SampleLayers::CurrentLayer);
  CHECK(patchy::sample_layers_from_settings(std::string_view(""), std::nullopt) == SampleLayers::AllLayers);
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

void collect_layer_descendant_ids_walks_nested_groups_in_order() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  const auto folder_id = folder.id();
  patchy::Layer first_child(document.allocate_layer_id(), "First", solid_rgb(2, 2, 10, 20, 30));
  const auto first_child_id = first_child.id();
  folder.add_child(std::move(first_child));
  patchy::Layer nested(document.allocate_layer_id(), "Nested", patchy::LayerKind::Group);
  const auto nested_id = nested.id();
  patchy::Layer nested_child(document.allocate_layer_id(), "Nested Child", solid_rgb(2, 2, 40, 50, 60));
  const auto nested_child_id = nested_child.id();
  nested.add_child(std::move(nested_child));
  folder.add_child(std::move(nested));
  patchy::Layer last_child(document.allocate_layer_id(), "Last", solid_rgb(2, 2, 70, 80, 90));
  const auto last_child_id = last_child.id();
  folder.add_child(std::move(last_child));
  document.add_layer(std::move(folder));

  std::vector<patchy::LayerId> ids;
  patchy::collect_layer_descendant_ids(*std::as_const(document).find_layer(folder_id), ids);
  const std::vector<patchy::LayerId> expected{first_child_id, nested_id, nested_child_id, last_child_id};
  CHECK(ids == expected);

  ids.clear();
  patchy::collect_layer_descendant_ids(*std::as_const(document).find_layer(first_child_id), ids);
  CHECK(ids.empty());
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

namespace {

std::vector<patchy::LayerId> sibling_ids(const std::vector<patchy::Layer>& layers) {
  std::vector<patchy::LayerId> ids;
  for (const auto& layer : layers) {
    ids.push_back(layer.id());
  }
  return ids;
}

}  // namespace

// GitHub issue 468: Bring to Front and Send to Back move within the layer's own folder, keep
// listed siblings in order, and never go under a pinned Background.
void arrange_layers_to_end_moves_within_each_folder() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  const auto background_id = document.add_pixel_layer("Background", solid_rgb(2, 2, 10, 20, 30)).id();
  const auto a_id = document.add_pixel_layer("A", solid_rgb(2, 2, 1, 2, 3)).id();
  const auto b_id = document.add_pixel_layer("B", solid_rgb(2, 2, 1, 2, 3)).id();
  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  const auto folder_id = folder.id();
  patchy::Layer c(document.allocate_layer_id(), "C", solid_rgb(2, 2, 1, 2, 3));
  const auto c_id = c.id();
  patchy::Layer d(document.allocate_layer_id(), "D", solid_rgb(2, 2, 1, 2, 3));
  const auto d_id = d.id();
  patchy::Layer e(document.allocate_layer_id(), "E", solid_rgb(2, 2, 1, 2, 3));
  const auto e_id = e.id();
  folder.add_child(std::move(c));
  folder.add_child(std::move(d));
  folder.add_child(std::move(e));
  document.add_layer(std::move(folder));
  const auto top_id = document.add_pixel_layer("Top", solid_rgb(2, 2, 1, 2, 3)).id();
  auto& layers = document.layers();
  using Ids = std::vector<patchy::LayerId>;

  // A and B to the front: above Top, A still under B.
  CHECK(patchy::arrange_layers_to_end(layers, {a_id, b_id}, true, background_id));
  CHECK(sibling_ids(layers) == (Ids{background_id, folder_id, top_id, a_id, b_id}));
  // Again: nothing changes.
  CHECK(!patchy::arrange_layers_to_end(layers, {a_id, b_id}, true, background_id));
  // To the back: just above the Background, which stays at the bottom.
  CHECK(patchy::arrange_layers_to_end(layers, {a_id, b_id}, false, background_id));
  CHECK(sibling_ids(layers) == (Ids{background_id, a_id, b_id, folder_id, top_id}));
  // Without a pinned Background they go to the very bottom.
  CHECK(patchy::arrange_layers_to_end(layers, {top_id}, false));
  CHECK(sibling_ids(layers) == (Ids{top_id, background_id, a_id, b_id, folder_id}));
  CHECK(patchy::arrange_layers_to_end(layers, {top_id}, true));
  // Inside the folder: C to the front of the folder, never out of it.
  CHECK(patchy::arrange_layers_to_end(layers, {c_id}, true, background_id));
  const auto* moved_folder = patchy::find_layer_in_tree(std::as_const(layers), folder_id);
  CHECK(moved_folder != nullptr && sibling_ids(moved_folder->children()) == (Ids{d_id, e_id, c_id}));
  CHECK(sibling_ids(layers) == (Ids{background_id, a_id, b_id, folder_id, top_id}));
  // E to the back of the folder.
  CHECK(patchy::arrange_layers_to_end(layers, {e_id}, false, background_id));
  moved_folder = patchy::find_layer_in_tree(std::as_const(layers), folder_id);
  CHECK(moved_folder != nullptr && sibling_ids(moved_folder->children()) == (Ids{e_id, d_id, c_id}));
  // A folder moves whole; a listed child inside it moves with it.
  CHECK(patchy::arrange_layers_to_end(layers, {folder_id, d_id}, false, background_id));
  CHECK(sibling_ids(layers) == (Ids{background_id, folder_id, a_id, b_id, top_id}));
  moved_folder = patchy::find_layer_in_tree(std::as_const(layers), folder_id);
  CHECK(moved_folder != nullptr && sibling_ids(moved_folder->children()) == (Ids{e_id, d_id, c_id}));
}

// GitHub issue 470: the footer's Alt-click Move Up / Move Down takes layers out of their folder,
// one level, just above or just below it.
void move_layers_out_of_folder_lands_beside_the_folder() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  const auto background_id = document.add_pixel_layer("Background", solid_rgb(2, 2, 10, 20, 30)).id();
  patchy::Layer outer(document.allocate_layer_id(), "Outer", patchy::LayerKind::Group);
  const auto outer_id = outer.id();
  patchy::Layer a(document.allocate_layer_id(), "A", solid_rgb(2, 2, 1, 2, 3));
  const auto a_id = a.id();
  patchy::Layer inner(document.allocate_layer_id(), "Inner", patchy::LayerKind::Group);
  const auto inner_id = inner.id();
  patchy::Layer b(document.allocate_layer_id(), "B", solid_rgb(2, 2, 1, 2, 3));
  const auto b_id = b.id();
  patchy::Layer c(document.allocate_layer_id(), "C", solid_rgb(2, 2, 1, 2, 3));
  const auto c_id = c.id();
  inner.add_child(std::move(b));
  inner.add_child(std::move(c));
  patchy::Layer d(document.allocate_layer_id(), "D", solid_rgb(2, 2, 1, 2, 3));
  const auto d_id = d.id();
  outer.add_child(std::move(a));
  outer.add_child(std::move(inner));
  outer.add_child(std::move(d));
  document.add_layer(std::move(outer));
  const auto top_id = document.add_pixel_layer("Top", solid_rgb(2, 2, 1, 2, 3)).id();
  auto& layers = document.layers();
  using Ids = std::vector<patchy::LayerId>;

  // A root layer has no folder to leave.
  CHECK(!patchy::move_layers_out_of_folder(layers, {top_id}, true));
  // A and D out of Outer, above it, in their order.
  CHECK(patchy::move_layers_out_of_folder(layers, {a_id, d_id}, true));
  CHECK(sibling_ids(layers) == (Ids{background_id, outer_id, a_id, d_id, top_id}));
  // B and C out of Inner (one level only), below it, still inside Outer.
  CHECK(patchy::move_layers_out_of_folder(layers, {b_id, c_id}, false));
  const auto* moved_outer = patchy::find_layer_in_tree(std::as_const(layers), outer_id);
  CHECK(moved_outer != nullptr && sibling_ids(moved_outer->children()) == (Ids{b_id, c_id, inner_id}));
  // Below Outer: above the Background, which stays at the bottom.
  CHECK(patchy::move_layers_out_of_folder(layers, {inner_id}, false));
  CHECK(sibling_ids(layers) == (Ids{background_id, inner_id, outer_id, a_id, d_id, top_id}));
  // B out of Outer, above it.
  CHECK(patchy::move_layers_out_of_folder(layers, {b_id}, true));
  CHECK(sibling_ids(layers) == (Ids{background_id, inner_id, outer_id, b_id, a_id, d_id, top_id}));
  // A listed child of a listed folder stays with the folder (Outer is at the root: nothing moves).
  CHECK(!patchy::move_layers_out_of_folder(layers, {outer_id, c_id}, true));
  moved_outer = patchy::find_layer_in_tree(std::as_const(layers), outer_id);
  CHECK(moved_outer != nullptr && sibling_ids(moved_outer->children()) == (Ids{c_id}));
}

// GitHub issue 389: the folders a merge stays inside.
void layer_shared_ancestor_groups_find_the_enclosing_folders() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(2, 2, 10, 20, 30));
  const auto background_id = std::as_const(document).layers()[0].id();
  patchy::Layer outer(document.allocate_layer_id(), "Outer", patchy::LayerKind::Group);
  const auto outer_id = outer.id();
  patchy::Layer inner(document.allocate_layer_id(), "Inner", patchy::LayerKind::Group);
  const auto inner_id = inner.id();
  patchy::Layer a(document.allocate_layer_id(), "A", solid_rgb(2, 2, 40, 50, 60));
  const auto a_id = a.id();
  patchy::Layer b(document.allocate_layer_id(), "B", solid_rgb(2, 2, 70, 80, 90));
  const auto b_id = b.id();
  patchy::Layer c(document.allocate_layer_id(), "C", solid_rgb(2, 2, 1, 2, 3));
  const auto c_id = c.id();
  inner.add_child(std::move(a));
  inner.add_child(std::move(b));
  outer.add_child(std::move(inner));
  outer.add_child(std::move(c));
  document.add_layer(std::move(outer));
  const auto& layers = std::as_const(document).layers();
  using Ids = std::set<patchy::LayerId>;
  CHECK((patchy::layer_shared_ancestor_groups(layers, {a_id, b_id}) == Ids{inner_id, outer_id}));
  CHECK((patchy::layer_shared_ancestor_groups(layers, {b_id, c_id}) == Ids{outer_id}));
  CHECK((patchy::layer_shared_ancestor_groups(layers, {inner_id, c_id}) == Ids{outer_id}));
  CHECK((patchy::layer_shared_ancestor_groups(layers, {a_id}) == Ids{inner_id, outer_id}));
  CHECK(patchy::layer_shared_ancestor_groups(layers, {a_id, background_id}).empty());
  CHECK(patchy::layer_shared_ancestor_groups(layers, {outer_id}).empty());
  CHECK(patchy::layer_shared_ancestor_groups(layers, {a_id, 99999}).empty());
  CHECK(patchy::layer_shared_ancestor_groups(layers, {}).empty());
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
  const auto& view = std::as_const(document);
  const auto background_id = view.layers()[0].id();
  const auto revision = view.layers()[1].content_revision();
  CHECK((patchy::root_drop_layer_ids(view.layers(), {child_id, 0, folder_id, background_id, folder_id}) ==
         std::vector<patchy::LayerId>{folder_id, background_id}));
  CHECK((patchy::root_drop_layer_ids(view.layers(), {child_id, background_id, child_id}) ==
         std::vector<patchy::LayerId>{child_id, background_id}));
  CHECK(patchy::root_drop_layer_ids(view.layers(), {folder_id, child_id, 99999}).empty());
  CHECK(view.layers()[1].content_revision() == revision);
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

void preview_scaled_document_shrinks_layers_and_styles() {
  patchy::Document document(301, 203, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer background(301, 203, patchy::PixelFormat::rgba8());
  background.clear(255);
  document.add_pixel_layer("Background", std::move(background));

  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  patchy::PixelBuffer red(33, 21, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < red.height(); ++y) {
    for (std::int32_t x = 0; x < red.width(); ++x) {
      auto* px = red.pixel(x, y);
      px[0] = 200;
      px[1] = 30;
      px[2] = 40;
      px[3] = 255;
    }
  }
  patchy::Layer child(document.allocate_layer_id(), "Styled Child", std::move(red));
  const auto child_id = child.id();
  child.set_bounds(patchy::Rect{-7, 13, 33, 21});
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.distance = 8.0F;
  shadow.size = 12.0F;
  child.layer_style().drop_shadows.push_back(shadow);
  patchy::PixelBuffer mask_pixels(33, 21, patchy::PixelFormat::gray8());
  mask_pixels.clear(255);
  child.set_mask(patchy::LayerMask{patchy::Rect{-7, 13, 33, 21}, std::move(mask_pixels), 0, false});
  folder.add_child(std::move(child));
  document.add_layer(std::move(folder));

  const auto scaled = patchy::build_preview_scaled_document(document, 2);
  CHECK(scaled.width() == patchy::preview_scaled_dimension(301, 2));
  CHECK(scaled.height() == patchy::preview_scaled_dimension(203, 2));
  CHECK(scaled.width() == 76);
  CHECK(scaled.height() == 51);

  const auto* scaled_child = scaled.find_layer(child_id);
  CHECK(scaled_child != nullptr);
  // floor(-7/4) = -2, floor(13/4) = 3; buffer 33x21 ceil-halves twice to 9x6,
  // and the scaled bounds must match the scaled buffer exactly.
  CHECK(scaled_child->bounds().x == -2);
  CHECK(scaled_child->bounds().y == 3);
  CHECK(scaled_child->bounds().width == scaled_child->pixels().width());
  CHECK(scaled_child->bounds().height == scaled_child->pixels().height());
  CHECK(scaled_child->pixels().width() == 9);
  CHECK(scaled_child->pixels().height() == 6);
  // Solid content survives box averaging untouched.
  const auto* scaled_px = scaled_child->pixels().pixel(4, 3);
  CHECK(static_cast<int>(scaled_px[0]) == 200);
  CHECK(static_cast<int>(scaled_px[3]) == 255);
  CHECK(scaled_child->layer_style().drop_shadows.size() == 1U);
  CHECK(scaled_child->layer_style().drop_shadows[0].distance == 2.0F);
  CHECK(scaled_child->layer_style().drop_shadows[0].size == 3.0F);
  CHECK(scaled_child->mask().has_value());
  CHECK(scaled_child->mask()->bounds.x == -2);
  CHECK(scaled_child->mask()->bounds.width == scaled_child->mask()->pixels.width());
  CHECK(scaled_child->mask()->pixels.width() == 9);
  CHECK(static_cast<int>(*scaled_child->mask()->pixels.pixel(4, 3)) == 255);
}

// The move preview's box downscale halves deep buffers per typed sample (it once walked
// them at one byte per channel, scattering 16 and 32-bit layers into shifted copies).
void preview_downscale_halves_deep_buffers_per_sample() {
  const auto float_format = patchy::with_bit_depth(patchy::PixelFormat::rgba8(), patchy::BitDepth::Float32);
  patchy::PixelBuffer linear(4, 2, float_format);
  auto* f = reinterpret_cast<float*>(linear.data().data());
  for (int i = 0; i < 8; ++i) {
    // Left 2x2 block: HDR red 4.0 opaque; right block: half-transparent blue at 0.5 alpha
    // beside fully transparent black, so the alpha weighting shows.
    const bool left = (i % 4) < 2;
    const bool transparent = !left && (i % 2) == 1;
    f[i * 4 + 0] = left ? 4.0F : 0.0F;
    f[i * 4 + 1] = 0.0F;
    f[i * 4 + 2] = left ? 0.0F : (transparent ? 0.0F : 0.8F);
    f[i * 4 + 3] = left ? 1.0F : (transparent ? 0.0F : 0.5F);
  }
  const auto halved = patchy::downscale_pixel_buffer_by_level(linear, 1);
  CHECK(halved.width() == 2 && halved.height() == 1);
  CHECK(halved.format() == float_format);
  const auto* h = reinterpret_cast<const float*>(std::as_const(halved).data().data());
  CHECK(std::abs(h[0] - 4.0F) < 1e-5F && std::abs(h[3] - 1.0F) < 1e-5F);
  // Alpha-weighted color keeps the blue at 0.8; alpha averages to 0.25.
  CHECK(std::abs(h[6] - 0.8F) < 1e-5F && std::abs(h[7] - 0.25F) < 1e-5F);

  const auto u16_format = patchy::with_bit_depth(patchy::PixelFormat::rgb8(), patchy::BitDepth::UInt16);
  patchy::PixelBuffer encoded(2, 2, u16_format);
  auto* u = reinterpret_cast<std::uint16_t*>(encoded.data().data());
  const std::array<std::uint16_t, 4> greens = {1000, 2000, 3000, 4001};
  for (int i = 0; i < 4; ++i) {
    u[i * 3 + 0] = 65535;
    u[i * 3 + 1] = greens[static_cast<std::size_t>(i)];
    u[i * 3 + 2] = 0;
  }
  const auto halved16 = patchy::downscale_pixel_buffer_by_level(encoded, 1);
  const auto* v = reinterpret_cast<const std::uint16_t*>(std::as_const(halved16).data().data());
  CHECK(v[0] == 65535 && v[2] == 0);
  CHECK(std::abs(static_cast<int>(v[1]) - 2500) <= 1);

  const auto mask_format = patchy::with_bit_depth(patchy::PixelFormat::gray8(), patchy::BitDepth::UInt16);
  patchy::PixelBuffer mask(2, 2, mask_format);
  auto* m = reinterpret_cast<std::uint16_t*>(mask.data().data());
  m[0] = 0;
  m[1] = 65535;
  m[2] = 65535;
  m[3] = 65535;
  const auto halved_mask = patchy::downscale_pixel_buffer_by_level(mask, 1);
  CHECK(std::abs(static_cast<int>(*reinterpret_cast<const std::uint16_t*>(std::as_const(halved_mask).data().data())) -
                 49151) <= 1);
}

// The RGBA8 flatten behind previews, script flatten and the flat writers accepts 16 and
// 32-bit documents (it threw on their deep layers) and returns their display values.
void rgba8_flatten_returns_display_values_of_deep_documents() {
  patchy::Document reference(24, 16, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer background(24, 16, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 16; ++y) {
    for (std::int32_t x = 0; x < 24; ++x) {
      auto* px = background.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x * 10);
      px[1] = static_cast<std::uint8_t>(y * 15);
      px[2] = 90;
      px[3] = 255;
    }
  }
  reference.add_pixel_layer("Background", std::move(background));
  const auto eight = patchy::flatten_document_rgba8(reference);
  for (const auto depth : {patchy::BitDepth::UInt16, patchy::BitDepth::Float32}) {
    auto deep = reference;
    patchy::convert_document_depth(deep, depth);
    const auto flat = patchy::flatten_document_rgba8(deep);
    CHECK(flat.format() == patchy::PixelFormat::rgba8());
    CHECK(flat.width() == 24 && flat.height() == 16);
    int worst = 0;
    for (std::int32_t y = 0; y < 16; ++y) {
      for (std::int32_t x = 0; x < 24; ++x) {
        for (int c = 0; c < 4; ++c) {
          worst = std::max(worst, std::abs(static_cast<int>(flat.pixel(x, y)[c]) - static_cast<int>(eight.pixel(x, y)[c])));
        }
      }
    }
    CHECK(worst <= 1);
    const auto rect = patchy::flatten_document_rgba8_rect(deep, patchy::Rect{5, 3, 10, 6});
    CHECK(rect.width() == 10 && rect.height() == 6);
    CHECK(std::abs(static_cast<int>(rect.pixel(0, 0)[0]) - static_cast<int>(eight.pixel(5, 3)[0])) <= 1);
  }
}

void preview_scaled_document_reuses_only_unchanged_surfaces() {
  patchy::Document document(32, 24, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(32, 24, patchy::PixelFormat::rgba8());
  pixels.clear(255);
  const auto id = document.add_pixel_layer("Cached", pixels).id();
  patchy::PixelBuffer mask(32, 24, patchy::PixelFormat::gray8());
  mask.clear(255);
  document.find_layer(id)->set_mask(patchy::LayerMask{patchy::Rect{0, 0, 32, 24}, mask, 0, false});
  patchy::PreviewScaleCache cache;
  const auto first = patchy::build_preview_scaled_document(document, 1, &cache);
  const auto* first_layer = first.find_layer(id);
  document.find_layer(id)->set_visible(false);
  document.find_layer(id)->set_bounds(patchy::Rect{7, -3, 32, 24});
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.size = 14;
  document.find_layer(id)->layer_style().drop_shadows.push_back(shadow);
  const auto second = patchy::build_preview_scaled_document(document, 1, &cache);
  const auto* second_layer = second.find_layer(id);
  CHECK(first_layer->pixels().data().data() == second_layer->pixels().data().data());
  CHECK(first_layer->mask()->pixels.data().data() == second_layer->mask()->pixels.data().data());
  CHECK(!second_layer->visible());
  CHECK(second_layer->bounds().x == 3 && second_layer->bounds().y == -2);
  CHECK(second_layer->layer_style().drop_shadows.front().size == 7);
  document.find_layer(id)->mask()->pixels.clear(0);
  const auto third = patchy::build_preview_scaled_document(document, 1, &cache);
  CHECK(third.find_layer(id)->pixels().data().data() == second_layer->pixels().data().data());
  CHECK(third.find_layer(id)->mask()->pixels.data().data() != second_layer->mask()->pixels.data().data());
  CHECK(*third.find_layer(id)->mask()->pixels.pixel(0, 0) == 0);
  document.find_layer(id)->pixels().clear(0);
  const auto fourth = patchy::build_preview_scaled_document(document, 1, &cache);
  CHECK(*fourth.find_layer(id)->pixels().pixel(0, 0) == 0);
  CHECK(*first_layer->pixels().pixel(0, 0) == 255);
  const auto smaller = patchy::build_preview_scaled_document(document, 2, &cache);
  CHECK(smaller.find_layer(id)->pixels().width() == 8);
  document.remove_layer(id);
  const auto removed = patchy::build_preview_scaled_document(document, 2, &cache);
  CHECK(removed.layers().empty());
  CHECK(cache.layers.empty());
}

void preview_scaled_document_flatten_keeps_solid_regions() {
  patchy::Document document(128, 96, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer background(128, 96, patchy::PixelFormat::rgba8());
  background.clear(255);
  document.add_pixel_layer("Background", std::move(background));
  patchy::PixelBuffer red(64, 48, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < red.height(); ++y) {
    for (std::int32_t x = 0; x < red.width(); ++x) {
      auto* px = red.pixel(x, y);
      px[0] = 210;
      px[1] = 40;
      px[2] = 50;
      px[3] = 255;
    }
  }
  patchy::Layer layer(document.allocate_layer_id(), "Solid", std::move(red));
  layer.set_bounds(patchy::Rect{32, 16, 64, 48});
  document.add_layer(std::move(layer));

  const auto scaled = patchy::build_preview_scaled_document(document, 1);
  CHECK(scaled.width() == 64);
  CHECK(scaled.height() == 48);
  const auto flattened = patchy::Compositor{}.flatten_rgb8(scaled);
  // Full-res red center (64, 40) sits at (32, 20) in the scaled composite;
  // solid regions are exact under box averaging.
  const auto* red_px = flattened.pixel(32, 20);
  CHECK(static_cast<int>(red_px[0]) == 210);
  CHECK(static_cast<int>(red_px[1]) == 40);
  const auto* white_px = flattened.pixel(4, 4);
  CHECK(static_cast<int>(white_px[0]) == 255);
}

// Ungroup keeps the composite order: [A, G{B, H{C}}, D] -> [A, B, H{C}, D],
// released ids top to bottom; a non-group refuses.
void ungroup_layer_releases_children_in_place_and_order() {
  patchy::Document document(8, 8, patchy::PixelFormat::rgba8());
  patchy::Layer a(document.allocate_layer_id(), "A", patchy::PixelBuffer());
  patchy::Layer b(document.allocate_layer_id(), "B", patchy::PixelBuffer());
  patchy::Layer c(document.allocate_layer_id(), "C", patchy::PixelBuffer());
  patchy::Layer d(document.allocate_layer_id(), "D", patchy::PixelBuffer());
  patchy::Layer inner(document.allocate_layer_id(), "H", patchy::LayerKind::Group);
  patchy::Layer group(document.allocate_layer_id(), "G", patchy::LayerKind::Group);
  const auto a_id = a.id();
  const auto b_id = b.id();
  const auto d_id = d.id();
  const auto inner_id = inner.id();
  const auto group_id = group.id();
  inner.add_child(std::move(c));
  group.add_child(std::move(b));
  group.add_child(std::move(inner));
  document.add_layer(std::move(a));
  document.add_layer(std::move(group));
  document.add_layer(std::move(d));

  CHECK(!patchy::ungroup_layer(document.layers(), a_id).has_value());
  CHECK(!patchy::ungroup_layer(document.layers(), 99999).has_value());
  const auto released = patchy::ungroup_layer(document.layers(), group_id);
  CHECK(released.has_value());
  CHECK((*released == std::vector<patchy::LayerId>{inner_id, b_id}));
  const auto& layers = document.layers();
  CHECK(layers.size() == 4);
  CHECK(layers[0].id() == a_id && layers[1].id() == b_id && layers[2].id() == inner_id && layers[3].id() == d_id);
  CHECK(layers[2].kind() == patchy::LayerKind::Group && layers[2].children().size() == 1);
  CHECK(document.find_layer(group_id) == nullptr);
}

}  // namespace

// GitHub issue 139: whole-image flips mirror layers, masks and channels about the
// canvas; Rotate 180 is two exact quarter turns.
void document_flips_and_half_turn_mirror_every_layer_and_channel() {
  patchy::Document document(10, 6, patchy::PixelFormat::rgba8());
  (void)document.add_pixel_layer("Background", solid_rgba(10, 6, 255, 255, 255, 255));
  auto pixels = solid_rgba(4, 2, 10, 20, 30, 255);
  pixels.pixel(0, 0)[0] = 200;  // a marker in the top-left corner
  // A layer smaller than the canvas: added canvas-sized, then given its own buffer.
  const auto layer_id = document.add_pixel_layer("Paint", solid_rgba(10, 6, 0, 0, 0, 0)).id();
  document.find_layer(layer_id)->set_pixels(std::move(pixels));
  document.find_layer(layer_id)->set_bounds(patchy::Rect{1, 1, 4, 2});

  patchy::flip_document_horizontal(document);
  {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer->bounds().x == 5 && layer->bounds().y == 1);  // 10 - 1 - 4
    CHECK(layer->pixels().pixel(3, 0)[0] == 200);              // the marker moved to the right edge
  }
  patchy::flip_document_horizontal(document);
  {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer->bounds().x == 1 && layer->pixels().pixel(0, 0)[0] == 200);
  }
  patchy::flip_document_vertical(document);
  {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer->bounds().y == 3 && layer->bounds().x == 1);  // 6 - 1 - 2
    CHECK(layer->pixels().pixel(0, 1)[0] == 200);
  }
  patchy::flip_document_vertical(document);
  patchy::rotate_document_180(document);
  CHECK(document.width() == 10 && document.height() == 6);
  {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer->bounds().x == 5 && layer->bounds().y == 3);  // mirrored on both axes
    CHECK(layer->pixels().pixel(3, 1)[0] == 200);
  }
}

// GitHub issue 139: a layer's quarter turn keeps its center and swaps its size.
void layer_quarter_turns_keep_the_layer_center() {
  patchy::Document document(40, 30, patchy::PixelFormat::rgba8());
  (void)document.add_pixel_layer("Background", solid_rgba(40, 30, 255, 255, 255, 255));
  auto pixels = solid_rgba(6, 2, 10, 20, 30, 255);
  pixels.pixel(0, 0)[0] = 200;  // top-left marker
  const auto layer_id = document.add_pixel_layer("Paint", solid_rgba(40, 30, 0, 0, 0, 0)).id();
  document.find_layer(layer_id)->set_pixels(std::move(pixels));
  document.find_layer(layer_id)->set_bounds(patchy::Rect{10, 8, 6, 2});  // center (13, 9)

  const auto affected = patchy::rotate_layer_clockwise(document, layer_id);
  CHECK(!affected.empty());
  {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer->bounds().width == 2 && layer->bounds().height == 6);
    CHECK(layer->bounds().x == 12 && layer->bounds().y == 6);  // still centered on (13, 9)
    CHECK(layer->pixels().pixel(1, 0)[0] == 200);              // top-left went to the top-right
  }
  (void)patchy::rotate_layer_counterclockwise(document, layer_id);
  {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer->bounds().x == 10 && layer->bounds().y == 8 && layer->bounds().width == 6);
    CHECK(layer->pixels().pixel(0, 0)[0] == 200);
  }
  (void)patchy::rotate_layer_180(document, layer_id);
  {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer->bounds().x == 10 && layer->bounds().y == 8);
    CHECK(layer->pixels().pixel(5, 1)[0] == 200);
  }
  // A document channel and an empty layer id are left alone.
  CHECK(patchy::rotate_layer_clockwise(document, patchy::LayerId{999999U}).empty());
}

std::vector<patchy::test::TestCase> document_model_tests() {
  return {
      {"layer_shared_ancestor_groups_find_the_enclosing_folders", layer_shared_ancestor_groups_find_the_enclosing_folders},
      {"document_flips_and_half_turn_mirror_every_layer_and_channel",
       document_flips_and_half_turn_mirror_every_layer_and_channel},
      {"layer_quarter_turns_keep_the_layer_center", layer_quarter_turns_keep_the_layer_center},
      {"pixel_buffer_tracks_shape_and_rows", pixel_buffer_tracks_shape_and_rows},
      {"pixel_buffer_copy_shares_storage_until_mutated", pixel_buffer_copy_shares_storage_until_mutated},
      {"document_snapshot_shares_pixels_when_only_moving_a_layer",
       document_snapshot_shares_pixels_when_only_moving_a_layer},
      {"document_memory_accounting_is_cow_aware", document_memory_accounting_is_cow_aware},
      {"stroke_patch_captures_and_restores_only_the_stroke_rect",
       stroke_patch_captures_and_restores_only_the_stroke_rect},
      {"document_channels_validate_crud_revisions_and_cow",
       document_channels_validate_crud_revisions_and_cow},
      {"channel_view_eyes_follow_the_composite_rules", channel_view_eyes_follow_the_composite_rules},
      {"document_channel_geometry_tracks_image_and_canvas_operations",
       document_channel_geometry_tracks_image_and_canvas_operations},
      {"document_adds_and_finds_layers", document_adds_and_finds_layers},
      {"document_removes_layers_and_updates_active_layer", document_removes_layers_and_updates_active_layer},
      {"document_can_clear_active_layer", document_can_clear_active_layer},
      {"default_non_group_layer_id_selects_topmost_visible_unlocked_pixel_child",
       default_non_group_layer_id_selects_topmost_visible_unlocked_pixel_child},
      {"only_layer_id_needs_exactly_one_layer_in_the_tree", only_layer_id_needs_exactly_one_layer_in_the_tree},
      {"default_non_group_layer_id_uses_visible_adjustment_before_hidden_pixels",
       default_non_group_layer_id_uses_visible_adjustment_before_hidden_pixels},
      {"sample_layers_excluded_ids_cover_current_and_below_and_no_adjustments",
       sample_layers_excluded_ids_cover_current_and_below_and_no_adjustments},
      {"sample_layers_composites_differ_per_choice", sample_layers_composites_differ_per_choice},
      {"sample_layers_settings_map_tokens_and_legacy_bool", sample_layers_settings_map_tokens_and_legacy_bool},
      {"default_non_group_layer_id_ignores_locked_content_and_folder_only_trees",
       default_non_group_layer_id_ignores_locked_content_and_folder_only_trees},
      {"default_non_group_layer_id_allows_position_locked_pixels",
       default_non_group_layer_id_allows_position_locked_pixels},
      {"layer_drop_request_moves_multiple_layers_into_folder", layer_drop_request_moves_multiple_layers_into_folder},
      {"arrange_layers_to_end_moves_within_each_folder", arrange_layers_to_end_moves_within_each_folder},
      {"move_layers_out_of_folder_lands_beside_the_folder", move_layers_out_of_folder_lands_beside_the_folder},
      {"collect_layer_descendant_ids_walks_nested_groups_in_order",
       collect_layer_descendant_ids_walks_nested_groups_in_order},
      {"layer_drop_roots_ignore_selected_descendants", layer_drop_roots_ignore_selected_descendants},
      {"document_print_settings_default_and_copy", document_print_settings_default_and_copy},
      {"document_grid_guides_default_and_copy", document_grid_guides_default_and_copy},
      {"preview_scaled_document_shrinks_layers_and_styles", preview_scaled_document_shrinks_layers_and_styles},
      {"preview_scaled_document_reuses_only_unchanged_surfaces", preview_scaled_document_reuses_only_unchanged_surfaces},
      {"preview_downscale_halves_deep_buffers_per_sample", preview_downscale_halves_deep_buffers_per_sample},
      {"rgba8_flatten_returns_display_values_of_deep_documents", rgba8_flatten_returns_display_values_of_deep_documents},
      {"preview_scaled_document_flatten_keeps_solid_regions", preview_scaled_document_flatten_keeps_solid_regions},
      {"ungroup_layer_releases_children_in_place_and_order", ungroup_layer_releases_children_in_place_and_order},
  };
}
