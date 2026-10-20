// Artboards (docs/artboards.md): the model, the layer-tree and geometry rules, the
// compositor against Photoshop's own composites, and the PSD 'artb'/'artd' records.
#include "core/artboard.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "core/pixel_tools.hpp"
#include "core/vector_shape.hpp"
#include "formats/document_flatten.hpp"
#include "formats/stb/stb_image.h"
#include "psd/psd_artboard.hpp"
#include "psd/psd_document_io.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using patchy::ArtboardBackground;
using patchy::Document;
using patchy::Layer;
using patchy::LayerId;
using patchy::LayerKind;
using patchy::PixelBuffer;
using patchy::PixelFormat;
using patchy::Rect;

PixelBuffer solid(std::int32_t width, std::int32_t height, std::array<std::uint8_t, 4> rgba) {
  PixelBuffer pixels(width, height, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = pixels.pixel(x, y);
      std::copy(rgba.begin(), rgba.end(), px);
    }
  }
  return pixels;
}

Layer pixel_layer(Document& document, const char* name, Rect bounds, std::array<std::uint8_t, 4> rgba) {
  Layer layer(document.allocate_layer_id(), name, solid(bounds.width, bounds.height, rgba));
  layer.set_bounds(bounds);
  return layer;
}

const std::uint8_t* at(const PixelBuffer& pixels, std::int32_t x, std::int32_t y) {
  return pixels.pixel(x, y);
}

// Two artboards with a gap, content spilling out of the first, and a loose layer under
// both: the Photoshop-calibrated scene of photoshop-artboard-clip.psd, built in code.
Document clip_scene() {
  Document document(750, 380, PixelFormat::rgba8());
  document.add_layer(pixel_layer(document, "Loose", Rect{150, 300, 500, 80}, {250, 200, 0, 255}));
  auto first = patchy::make_artboard_group(document.allocate_layer_id(), "A1", Rect{0, 0, 300, 250});
  first.add_child(pixel_layer(document, "Spill", Rect{200, 50, 300, 50}, {220, 30, 30, 255}));
  document.add_layer(std::move(first));
  auto second = patchy::make_artboard_group(document.allocate_layer_id(), "A2", Rect{450, 0, 300, 250});
  second.set_opacity(128.0F / 255.0F);
  second.set_blend_mode(patchy::BlendMode::Multiply);
  document.add_layer(std::move(second));
  return document;
}

void artboard_model_names_order_and_lookup() {
  Document document(1000, 600, PixelFormat::rgba8());
  CHECK(!patchy::document_has_artboards(document));
  CHECK(patchy::next_artboard_name(document) == "Artboard 1");
  CHECK(!patchy::artboard_union_bounds(document).has_value());
  CHECK(patchy::artboard_rect_beside(document, 200, 100) == (Rect{0, 0, 200, 100}));

  auto first = patchy::make_artboard_group(document.allocate_layer_id(), "Artboard 1", Rect{0, 0, 300, 200});
  // Photoshop makes artboards Normal (isolated), not Pass Through like a plain group.
  CHECK(first.blend_mode() == patchy::BlendMode::Normal);
  CHECK(first.artboard()->background == ArtboardBackground::White);
  const auto first_id = first.id();
  first.add_child(pixel_layer(document, "Child", Rect{10, 10, 20, 20}, {0, 0, 255, 255}));
  const auto child_id = first.children().front().id();
  document.add_layer(std::move(first));
  document.add_layer(pixel_layer(document, "Loose", Rect{0, 400, 10, 10}, {0, 0, 0, 255}));
  auto third = patchy::make_artboard_group(document.allocate_layer_id(), "Artboard 7", Rect{400, 0, 200, 200},
                                           ArtboardBackground::Custom, {10, 20, 30});
  const auto third_id = third.id();
  document.add_layer(std::move(third));

  CHECK(patchy::document_has_artboards(document));
  const auto boards = patchy::document_artboards(document);
  CHECK(boards.size() == 2U);
  if (boards.size() == 2U) {
    CHECK(boards[0]->id() == third_id);  // top of the stack first
    CHECK(boards[1]->id() == first_id);
  }
  CHECK(patchy::next_artboard_name(document) == "Artboard 8");
  CHECK(patchy::artboard_union_bounds(document) == (Rect{0, 0, 600, 200}));
  // The loose layer counts toward the extent Photoshop shrinkwraps to.
  CHECK(patchy::artboard_document_extent(document) == (Rect{0, 0, 600, 410}));
  CHECK(patchy::artboard_at(document, 450, 50) != nullptr && patchy::artboard_at(document, 450, 50)->id() == third_id);
  CHECK(patchy::artboard_at(document, 350, 50) == nullptr);
  CHECK(patchy::artboard_owning_layer(document, child_id) != nullptr &&
        patchy::artboard_owning_layer(document, child_id)->id() == first_id);
  CHECK(patchy::artboard_owning_layer(document, first_id)->id() == first_id);
  // Right of the rightmost, 100 px apart, top-aligned.
  CHECK(patchy::artboard_rect_beside(document, 50, 60) == (Rect{700, 0, 50, 60}));
  CHECK(patchy::artboard_rect_next_to(Rect{100, 100, 50, 50}, 1, 40, 30) == (Rect{100, 250, 40, 30}));
  CHECK(patchy::artboard_rect_next_to(Rect{100, 100, 50, 50}, 2, 40, 30) == (Rect{-40, 100, 40, 30}));
  CHECK(patchy::artboard_rect_next_to(Rect{100, 100, 50, 50}, 3, 40, 30) == (Rect{100, -30, 40, 30}));

  // Only a group can be an artboard.
  Layer pixel(document.allocate_layer_id(), "Pixel", LayerKind::Pixel);
  bool threw = false;
  try {
    pixel.set_artboard(patchy::LayerArtboard{});
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(threw);

  // Moving an artboard's rect is a render-only change; clearing makes it a plain group.
  auto* board = document.find_layer(first_id);
  const auto content_before = std::as_const(*board).content_revision();
  board->translate_artboard(5, 6);
  CHECK(std::as_const(*board).artboard()->rect == (Rect{5, 6, 300, 200}));
  CHECK(std::as_const(*board).content_revision() == content_before);
  board->clear_artboard();
  CHECK(!patchy::layer_is_artboard(std::as_const(*board)));
}

void artboard_layer_tree_keeps_artboards_top_level() {
  Document document(800, 400, PixelFormat::rgba8());
  auto board = patchy::make_artboard_group(document.allocate_layer_id(), "Artboard 1", Rect{0, 0, 300, 300});
  const auto board_id = board.id();
  document.add_layer(std::move(board));
  Layer group(document.allocate_layer_id(), "Group", LayerKind::Group);
  const auto group_id = group.id();
  group.add_child(pixel_layer(document, "Inside", Rect{0, 0, 5, 5}, {1, 2, 3, 255}));
  const auto inside_id = group.children().front().id();
  document.add_layer(std::move(group));
  auto loose = pixel_layer(document, "Loose", Rect{0, 0, 5, 5}, {1, 2, 3, 255});
  const auto loose_id = loose.id();
  document.add_layer(std::move(loose));

  // An artboard cannot go into a group, nor next to a layer inside one.
  patchy::LayerDropRequest into_group;
  into_group.layer_ids_top_to_bottom = {board_id};
  into_group.target_layer_id = group_id;
  into_group.position = patchy::LayerDropPosition::OnItem;
  CHECK(!patchy::move_layers_for_drop(document.layers(), into_group));
  patchy::LayerDropRequest beside_nested;
  beside_nested.layer_ids_top_to_bottom = {board_id};
  beside_nested.target_layer_id = inside_id;
  beside_nested.position = patchy::LayerDropPosition::AboveItem;
  CHECK(!patchy::move_layers_for_drop(document.layers(), beside_nested));
  CHECK(document.layers().front().id() == board_id);
  // Reordering among the top-level layers works.
  patchy::LayerDropRequest reorder;
  reorder.layer_ids_top_to_bottom = {board_id};
  reorder.target_layer_id = loose_id;
  reorder.position = patchy::LayerDropPosition::AboveItem;
  CHECK(patchy::move_layers_for_drop(document.layers(), reorder));
  CHECK(document.layers().back().id() == board_id);
  // Ordinary layers go into an artboard like into any group.
  patchy::LayerDropRequest into_board;
  into_board.layer_ids_top_to_bottom = {loose_id};
  into_board.target_layer_id = board_id;
  into_board.position = patchy::LayerDropPosition::OnItem;
  CHECK(patchy::move_layers_for_drop(document.layers(), into_board));
  CHECK(patchy::artboard_owning_layer(document, loose_id) != nullptr);
}

void artboard_rects_follow_canvas_geometry() {
  const auto make = [] {
    Document document(400, 200, PixelFormat::rgba8());
    document.add_layer(patchy::make_artboard_group(document.allocate_layer_id(), "A", Rect{10, 20, 100, 50}));
    return document;
  };
  const auto rect_of = [](const Document& document) { return document.layers().front().artboard()->rect; };
  {
    auto document = make();
    patchy::resize_canvas_to_frame(document, Rect{-30, -40, 500, 300});
    CHECK(rect_of(document) == (Rect{40, 60, 100, 50}));
  }
  {
    auto document = make();
    patchy::flip_document_horizontal(document);
    CHECK(rect_of(document) == (Rect{290, 20, 100, 50}));
  }
  {
    auto document = make();
    patchy::flip_document_vertical(document);
    CHECK(rect_of(document) == (Rect{10, 130, 100, 50}));
  }
  {
    auto document = make();
    patchy::rotate_document_clockwise(document);
    CHECK(document.width() == 200 && document.height() == 400);
    CHECK(rect_of(document) == (Rect{130, 10, 50, 100}));
  }
  {
    auto document = make();
    patchy::resize_image_and_layers(document, 800, 400, patchy::ResampleMethod::NearestNeighbor);
    CHECK(rect_of(document) == (Rect{20, 40, 200, 100}));
  }
  {
    auto document = make();
    CHECK(patchy::crop_document(document, Rect{5, 5, 200, 100}, patchy::EditColor{0, 0, 0, 0}));
    CHECK(rect_of(document) == (Rect{5, 15, 100, 50}));
  }
}

void artboard_fit_and_grow_canvas_move_content_and_guides() {
  Document document(300, 300, PixelFormat::rgba8());
  document.guides().push_back({patchy::GuideOrientation::Vertical, 50 * 32});
  auto board = patchy::make_artboard_group(document.allocate_layer_id(), "A", Rect{100, 100, 100, 100});
  board.add_child(pixel_layer(document, "Dot", Rect{120, 120, 10, 10}, {9, 9, 9, 255}));
  document.add_layer(std::move(board));
  auto second = patchy::make_artboard_group(document.allocate_layer_id(), "B", Rect{250, -50, 100, 100});
  document.add_layer(std::move(second));

  // Grow never shrinks: the canvas takes in B (to x 350, y -50) and keeps the rest.
  const auto grown = patchy::grow_canvas_to_artboards(document);
  CHECK(grown.has_value() && (*grown)[0] == 0 && (*grown)[1] == 50);
  CHECK(document.width() == 350 && document.height() == 350);
  CHECK(document.layers()[1].artboard()->rect == (Rect{250, 0, 100, 100}));
  CHECK(document.layers()[0].children().front().bounds() == (Rect{120, 170, 10, 10}));
  CHECK(!patchy::grow_canvas_to_artboards(document).has_value());  // nothing left to take in

  // Fit is Photoshop's shrinkwrap: exactly the artboards' extent.
  const auto fitted = patchy::fit_canvas_to_artboards(document);
  CHECK(fitted.has_value() && (*fitted)[0] == -100 && (*fitted)[1] == 0);
  CHECK(document.width() == 250 && document.height() == 250);
  CHECK(document.layers()[0].artboard()->rect == (Rect{0, 150, 100, 100}));
  CHECK(document.layers()[0].children().front().bounds() == (Rect{20, 170, 10, 10}));
  CHECK(document.guides().front().position_32 == -50 * 32);
}

// The compositor's artboard rules, on the scene Photoshop composited for
// photoshop-artboard-clip.psd: content clips to its artboard (a spill never shows in the
// gap or on the next artboard), the white paper paints under the children, loose layers
// render outside every artboard, and a 50% artboard shows its paper at alpha 128.
void artboard_compositor_clips_children_and_paints_backgrounds() {
  const auto document = clip_scene();
  const auto composite = patchy::flatten_document_rgba8(document);
  CHECK(composite.width() == 750 && composite.height() == 380);
  const auto expect = [&](std::int32_t x, std::int32_t y, std::array<int, 4> rgba) {
    const auto* px = at(composite, x, y);
    for (int channel = 0; channel < 4; ++channel) {
      if (std::abs(px[channel] - rgba[static_cast<std::size_t>(channel)]) > 1) {
        std::printf("  pixel (%d, %d) = %d %d %d %d, expected %d %d %d %d\n", x, y, px[0], px[1], px[2], px[3],
                    rgba[0], rgba[1], rgba[2], rgba[3]);
        CHECK(false);
        return;
      }
    }
  };
  expect(10, 10, {255, 255, 255, 255});   // A1's paper
  expect(250, 75, {220, 30, 30, 255});    // the spill inside A1
  expect(350, 75, {0, 0, 0, 0});          // ...clipped in the gap
  expect(470, 75, {255, 255, 255, 128});  // A2's paper at 50%, not the spill
  expect(200, 340, {250, 200, 0, 255});   // the loose layer outside every artboard
  expect(400, 270, {0, 0, 0, 0});         // nothing at all

  // Each background type (Transparent paints nothing; Custom its color).
  Document types(400, 100, PixelFormat::rgba8());
  const std::array<ArtboardBackground, 4> kinds{ArtboardBackground::White, ArtboardBackground::Black,
                                                 ArtboardBackground::Transparent, ArtboardBackground::Custom};
  for (int index = 0; index < 4; ++index) {
    types.add_layer(patchy::make_artboard_group(types.allocate_layer_id(), "T", Rect{index * 100, 0, 100, 100},
                                                kinds[static_cast<std::size_t>(index)], {30, 120, 200}));
  }
  const auto swatches = patchy::flatten_document_rgba8(types);
  const auto* white = at(swatches, 50, 50);
  const auto* black = at(swatches, 150, 50);
  const auto* clear = at(swatches, 250, 50);
  const auto* custom = at(swatches, 350, 50);
  CHECK(white[0] == 255 && white[3] == 255);
  CHECK(black[0] == 0 && black[1] == 0 && black[2] == 0 && black[3] == 255);
  CHECK(clear[3] == 0);
  CHECK(custom[0] == 30 && custom[1] == 120 && custom[2] == 200 && custom[3] == 255);
}

// The plain-layer form writers without artboards get composites exactly like the artboards.
void artboard_plain_group_conversion_composites_the_same() {
  const auto document = clip_scene();
  auto plain = document;
  patchy::convert_artboards_to_plain_groups(plain);
  CHECK(!patchy::document_has_artboards(plain));
  const auto* first = &plain.layers()[1];
  CHECK(first->vector_mask() != nullptr);
  CHECK(!first->children().empty() && first->children().front().name() == "A1 Background");
  const auto expected = patchy::flatten_document_rgba8(document);
  const auto actual = patchy::flatten_document_rgba8(plain);
  int worst = 0;
  for (std::int32_t y = 0; y < expected.height(); ++y) {
    for (std::int32_t x = 0; x < expected.width(); ++x) {
      for (int channel = 0; channel < 4; ++channel) {
        worst = std::max(worst, std::abs(at(expected, x, y)[channel] - at(actual, x, y)[channel]));
      }
    }
  }
  CHECK(worst <= 1);
}

PixelBuffer read_png_rgba(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  int width = 0;
  int height = 0;
  int components = 0;
  stbi_uc* decoded =
      stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &components, 4);
  PixelBuffer pixels;
  if (decoded != nullptr) {
    pixels = PixelBuffer(width, height, PixelFormat::rgba8());
    std::copy(decoded, decoded + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U,
              pixels.data().begin());
  }
  stbi_image_free(decoded);
  return pixels;
}

// Photoshop's own artboard files render like Photoshop's own composites (the PNG beside
// each fixture is Photoshop's flatten with transparency, October 2026).
void artboard_photoshop_fixtures_render_like_photoshop() {
  for (const char* name : {"photoshop-artboard-backgrounds", "photoshop-artboard-clip"}) {
    const auto document =
        patchy::psd::DocumentIo::read_file(patchy::test::committed_psd_fixture_path(std::string(name) + ".psd"));
    const auto expected = read_png_rgba(patchy::test::committed_psd_fixture_path(std::string(name) + ".png"));
    CHECK(patchy::document_has_artboards(document));
    CHECK(!expected.empty());
    const auto actual = patchy::flatten_document_rgba8(document);
    CHECK(actual.width() == expected.width() && actual.height() == expected.height());
    if (actual.width() != expected.width() || actual.height() != expected.height()) {
      continue;
    }
    int worst = 0;
    for (std::int32_t y = 0; y < actual.height(); ++y) {
      for (std::int32_t x = 0; x < actual.width(); ++x) {
        const auto* a = at(actual, x, y);
        const auto* e = at(expected, x, y);
        worst = std::max(worst, std::abs(a[3] - e[3]));
        if (e[3] > 0 && a[3] > 0) {
          for (int channel = 0; channel < 3; ++channel) {
            worst = std::max(worst, std::abs(a[channel] - e[channel]));
          }
        }
      }
    }
    if (worst > 1) {
      std::printf("  %s: worst channel delta %d\n", name, worst);
    }
    CHECK(worst <= 1);
  }
}

const patchy::UnknownPsdBlock* block_with_key(const Layer& layer, std::string_view key) {
  for (const auto& block : layer.unknown_psd_blocks()) {
    if (block.key == key) {
      return &block;
    }
  }
  return nullptr;
}

// Photoshop's records parse into the model, and the model regenerates them to the byte:
// Patchy-authored 'artb' and 'artd' are exactly what Photoshop writes.
void psd_artboard_blocks_regenerate_photoshop_bytes() {
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-artboard-backgrounds.psd"));
  const auto boards = patchy::document_artboards(document);
  CHECK(boards.size() == 4U);
  const std::array<ArtboardBackground, 4> types_bottom_up{ArtboardBackground::White, ArtboardBackground::Black,
                                                          ArtboardBackground::Transparent, ArtboardBackground::Custom};
  int index = 0;
  for (const auto& layer : document.layers()) {
    if (!patchy::layer_is_artboard(layer)) {
      continue;
    }
    const auto& artboard = *layer.artboard();
    CHECK(artboard.background == types_bottom_up[static_cast<std::size_t>(index)]);
    CHECK(artboard.rect == (Rect{index * 250, 0, 200, 200}));
    const auto* block = block_with_key(layer, "artb");
    CHECK(block != nullptr);
    if (block != nullptr) {
      CHECK(patchy::psd::artboard_block_payload(artboard) == block->payload);
    }
    ++index;
  }
  CHECK(index == 4);
  CHECK(document.layers().back().artboard()->color == (patchy::RgbColor{30, 120, 200}));
  CHECK(document.metadata().artboard_settings.has_value());
  const patchy::UnknownPsdBlock* artd = nullptr;
  for (const auto& block : document.metadata().unknown_psd_resources) {
    if (block.key == "artd") {
      artd = &block;
    }
  }
  CHECK(artd != nullptr);
  if (artd != nullptr && document.metadata().artboard_settings.has_value()) {
    const auto parsed = patchy::psd::parse_document_artboard_block(artd->payload);
    CHECK(parsed.has_value() && parsed->count == 4);
    CHECK(patchy::psd::document_artboard_block_payload(*document.metadata().artboard_settings, 4) == artd->payload);
  }
}

std::vector<const patchy::UnknownPsdBlock*> artboard_blocks(const Document& document) {
  std::vector<const patchy::UnknownPsdBlock*> blocks;
  for (const auto& layer : document.layers()) {
    for (const auto& block : layer.unknown_psd_blocks()) {
      if (patchy::psd::is_artboard_block_key(block.key)) {
        blocks.push_back(&block);
      }
    }
  }
  return blocks;
}

// Every PSD these tests write is also kept in test-artifacts/artboards for the Photoshop
// open check (scripts\dev\photoshop-open-check.ps1, docs/ps-compat.md).
void keep_psd_artifact(const char* name, const std::vector<std::uint8_t>& bytes) {
  const auto dir = std::filesystem::path("test-artifacts") / "artboards";
  std::filesystem::create_directories(dir);
  std::ofstream out(dir / name, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// Save and reopen: an unedited artboard writes its record verbatim, an edit regenerates
// it, a group that stopped being an artboard loses it, and a Patchy-born artboard gets
// both records.
void psd_artboard_round_trip_and_edits() {
  auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-artboard-clip.psd"));
  {
    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    keep_psd_artifact("patchy-artboard-resaved.psd", bytes);
    const auto reopened = patchy::psd::DocumentIo::read(bytes);
    CHECK(patchy::document_artboards(reopened).size() == 2U);
    const auto before = artboard_blocks(document);
    const auto after = artboard_blocks(reopened);
    CHECK(before.size() == after.size());
    for (std::size_t i = 0; i < before.size() && i < after.size(); ++i) {
      CHECK(before[i]->payload == after[i]->payload);
    }
    CHECK(reopened.metadata().artboard_settings == document.metadata().artboard_settings);
  }
  // Move A1 and turn A2 back into a plain group.
  for (auto& layer : document.layers()) {
    if (layer.name() == "A1") {
      layer.translate_artboard(7, 0);
    } else if (layer.name() == "A2") {
      layer.clear_artboard();
    }
  }
  {
    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    keep_psd_artifact("patchy-artboard-edited.psd", bytes);
    const auto reopened = patchy::psd::DocumentIo::read(bytes);
    const auto boards = patchy::document_artboards(reopened);
    CHECK(boards.size() == 1U);
    if (boards.size() == 1U) {
      CHECK(boards.front()->name() == "A1");
      CHECK(boards.front()->artboard()->rect == (Rect{7, 0, 300, 250}));
    }
    CHECK(artboard_blocks(reopened).size() == 1U);
    const patchy::UnknownPsdBlock* artd = nullptr;
    for (const auto& block : reopened.metadata().unknown_psd_resources) {
      if (block.key == "artd") {
        artd = &block;
      }
    }
    CHECK(artd != nullptr && patchy::psd::parse_document_artboard_block(artd->payload)->count == 1);
  }

  // A Patchy-born document.
  auto born = clip_scene();
  // Every background kind on one canvas, and an empty artboard.
  {
    Document backgrounds(820, 200, PixelFormat::rgba8());
    std::int32_t x = 0;
    for (const auto background : {ArtboardBackground::White, ArtboardBackground::Black,
                                  ArtboardBackground::Transparent, ArtboardBackground::Custom}) {
      auto board = patchy::make_artboard_group(backgrounds.allocate_layer_id(), patchy::next_artboard_name(backgrounds),
                                               Rect{x, 0, 180, 200}, background, patchy::RgbColor{40, 120, 200});
      if (background != ArtboardBackground::Transparent) {
        board.add_child(pixel_layer(backgrounds, "Mark", Rect{x + 20, 20, 60, 60}, {250, 120, 0, 255}));
      }
      backgrounds.add_layer(std::move(board));
      x += 210;
    }
    backgrounds.metadata().artboard_settings.emplace();
    const auto written = patchy::psd::DocumentIo::write_layered_rgb8(backgrounds);
    keep_psd_artifact("patchy-artboard-backgrounds.psd", written);
    const auto back = patchy::psd::DocumentIo::read(written);
    const auto kinds = patchy::document_artboards(back);
    CHECK(kinds.size() == 4U);
    if (kinds.size() == 4U) {
      CHECK(kinds[0]->artboard()->background == ArtboardBackground::Custom);
      CHECK(kinds[0]->artboard()->color == (patchy::RgbColor{40, 120, 200}));
      CHECK(kinds[2]->artboard()->background == ArtboardBackground::Black);
    }
  }
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(born);
  keep_psd_artifact("patchy-artboard-born.psd", bytes);
  const auto reopened = patchy::psd::DocumentIo::read(bytes);
  const auto boards = patchy::document_artboards(reopened);
  CHECK(boards.size() == 2U);
  if (boards.size() == 2U) {
    CHECK(boards[0]->name() == "A2" && boards[0]->blend_mode() == patchy::BlendMode::Multiply);
    CHECK(boards[1]->artboard()->rect == (Rect{0, 0, 300, 250}));
  }
  CHECK(reopened.metadata().artboard_settings.has_value());
  // And a document with no artboards writes no artboard record at all.
  Document plain(10, 10, PixelFormat::rgba8());
  plain.add_layer(pixel_layer(plain, "P", Rect{0, 0, 10, 10}, {1, 1, 1, 255}));
  const auto plain_bytes = patchy::psd::DocumentIo::write_layered_rgb8(plain);
  const std::string_view text(reinterpret_cast<const char*>(plain_bytes.data()), plain_bytes.size());
  CHECK(text.find("artb") == std::string_view::npos && text.find("artd") == std::string_view::npos);
}

// The older psd-tools fixtures (local only): every artboard file opens with its artboards,
// including the ones whose records leave out the background type or the guide list.
void psd_artboard_local_fixtures_open_if_available() {
  const auto root = patchy::test::local_format_fixture_path("psd-tools", "tests/psd_files");
  patchy::test::skip_unless_exists(root);
  std::size_t checked = 0;
  for (const char* name : {"artboard.psd", "artboard-bgcolor.psd", "gradient-sizes.psd"}) {
    const auto path = root / name;
    if (!std::filesystem::exists(path)) {
      continue;
    }
    ++checked;
    const auto document = patchy::psd::DocumentIo::read_file(path);
    CHECK(patchy::document_has_artboards(document));
  }
  if (checked == 0) {
    patchy::test::skip("no optional fixture present (" + patchy::test::fixture_display_path(root) + ")");
  }
}

// Artboard from Layers (menu and scripts): stacking order kept whatever order the ids
// come in, sized to the layers, on top of the stack; a refusal changes nothing.
void artboard_put_layers_on_new_artboard_keeps_order_and_refuses() {
  Document document(400, 300, PixelFormat::rgba8());
  document.add_layer(pixel_layer(document, "Bottom", Rect{10, 20, 50, 40}, {255, 0, 0, 255}));
  document.add_layer(pixel_layer(document, "Middle", Rect{200, 100, 30, 30}, {0, 255, 0, 255}));
  document.add_layer(pixel_layer(document, "Top", Rect{300, 250, 20, 20}, {0, 0, 255, 255}));
  const auto bottom = document.layers()[0].id();
  const auto top = document.layers()[2].id();
  const auto id = patchy::put_layers_on_new_artboard(document, {top, bottom});
  CHECK(id.has_value());
  if (!id.has_value()) {
    return;
  }
  CHECK(document.layers().size() == 2);
  const auto& artboard = document.layers().back();
  CHECK(artboard.id() == *id && artboard.name() == "Artboard 1");
  CHECK(artboard.artboard()->rect == (Rect{10, 20, 310, 250}));
  CHECK(artboard.children().size() == 2 && artboard.children()[0].id() == bottom && artboard.children()[1].id() == top);
  CHECK(document.layers().front().name() == "Middle");

  // Already on an artboard, an artboard itself, or missing: nothing moves.
  const auto before = document.layers().size();
  CHECK(!patchy::put_layers_on_new_artboard(document, {bottom}).has_value());
  CHECK(!patchy::put_layers_on_new_artboard(document, {*id}).has_value());
  CHECK(!patchy::put_layers_on_new_artboard(document, {987654}).has_value());
  CHECK(!patchy::put_layers_on_new_artboard(document, {}).has_value());
  CHECK(document.layers().size() == before);
  const auto middle = document.layers().front().id();
  const auto named = patchy::put_layers_on_new_artboard(document, {middle}, "Hero");
  CHECK(named.has_value() && document.find_layer(*named)->name() == "Hero");
}

// The canvas-growth limits: growth stops at kMaxArtboardCanvasSide (callers refuse the
// edit), a bottom "Background" that widened with the canvas never keeps it from shrinking
// back, and an artboard that already has a vector mask still clips to its rect once it is
// converted for writers without artboards.
void artboard_canvas_limits_background_and_masked_conversion() {
  Document document(400, 300, PixelFormat::rgba8());
  document.add_layer(pixel_layer(document, "Background", Rect{0, 0, 400, 300}, {255, 255, 255, 255}));
  auto board = patchy::make_artboard_group(document.allocate_layer_id(), "A", Rect{50, 50, 100, 100});
  board.add_child(pixel_layer(document, "Wide", Rect{0, 60, 400, 20}, {200, 0, 0, 255}));
  document.add_layer(std::move(board));
  CHECK(patchy::artboard_canvas_within_limit(document));
  CHECK(!patchy::artboard_canvas_would_grow(document));

  // Past the left edge: the canvas grows, the Background widens with it...
  document.layers().back().translate_artboard(-500, 0);
  CHECK(patchy::artboard_canvas_would_grow(document));
  const auto shift = patchy::grow_canvas_to_artboards(document);
  CHECK(shift.has_value() && (*shift)[0] == 450);
  CHECK(document.width() == 850);
  // ...and moving the artboard back lets Fit Canvas shrink to the artboard alone.
  document.layers().back().translate_artboard(450, 0);
  const auto fit = patchy::fit_canvas_to_artboards(document);
  CHECK(fit.has_value());
  CHECK(document.width() == 100 && document.height() == 100);

  // Far away: refused, nothing changes.
  document.layers().back().translate_artboard(40000, 0);
  CHECK(!patchy::artboard_canvas_within_limit(document));
  CHECK(!patchy::grow_canvas_to_artboards(document).has_value());
  CHECK(document.width() == 100);
  document.layers().back().translate_artboard(-40000, 0);

  // A masked artboard converts to a group whose inner group carries the rect clip.
  auto& artboard = document.layers().back();
  patchy::LayerVectorMask own;
  own.path = patchy::VectorPath{};
  artboard.set_vector_mask(std::move(own));
  auto plain = document;
  patchy::convert_artboards_to_plain_groups(plain);
  const auto& converted = plain.layers().back();
  CHECK(converted.vector_mask() != nullptr);
  CHECK(converted.children().size() == 1U);
  CHECK(converted.children().size() == 1U && converted.children()[0].name() == "A Clip" &&
        converted.children()[0].vector_mask() != nullptr);
}

// flatten_document_rgba8_rect (the editable PDF writer's per-chunk and banded composite)
// equals the same rect cut from the full flatten, across artboard clips, a blended and
// faded artboard, spill and a loose layer.
// Prevent Auto-Nesting (GitHub issue 246): a locked layer never changes artboard on a
// move, and nothing nests into or out of a locked artboard.
void artboard_nest_moves_honor_prevent_auto_nesting() {
  Document document(800, 400, PixelFormat::rgba8());
  const auto loose = document.add_layer(pixel_layer(document, "Loose", Rect{20, 300, 40, 40}, {0, 0, 255, 255})).id();
  auto first = patchy::make_artboard_group(document.allocate_layer_id(), "A1", Rect{0, 0, 300, 250});
  auto inside_layer = pixel_layer(document, "Inside", Rect{50, 50, 40, 40}, {255, 0, 0, 255});
  const auto inside = inside_layer.id();
  first.add_child(std::move(inside_layer));
  const auto board_a = document.add_layer(std::move(first)).id();
  const auto board_b =
      document.add_layer(patchy::make_artboard_group(document.allocate_layer_id(), "A2", Rect{450, 0, 300, 250})).id();
  using Move = patchy::ArtboardNestMove;
  const auto moves = [&document](LayerId id, std::int32_t dx, std::int32_t dy) {
    return patchy::artboard_nest_moves(document, {id}, dx, dy);
  };
  const auto lock = [&document](LayerId id, bool locked) {
    auto* layer = document.find_layer(id);
    CHECK(layer != nullptr);
    patchy::set_layer_prevents_auto_nesting(*layer, locked);
    CHECK(patchy::layer_prevents_auto_nesting(*layer) == locked);
    // Not an editing lock.
    CHECK(!patchy::layer_locks_all(*layer));
    CHECK(!patchy::layer_effectively_locks_position(document.layers(), id));
  };

  // Unlocked: into A1, from A1 into A2, and out to the top level.
  CHECK(moves(loose, 0, -250) == (std::vector<Move>{{loose, board_a}}));
  CHECK(moves(inside, 450, 0) == (std::vector<Move>{{inside, board_b}}));
  CHECK(moves(inside, 0, 300) == (std::vector<Move>{{inside, std::nullopt}}));

  // A locked layer stays where it is.
  lock(loose, true);
  lock(inside, true);
  CHECK(moves(loose, 0, -250).empty());
  CHECK(moves(inside, 450, 0).empty());
  CHECK(moves(inside, 0, 300).empty());
  lock(loose, false);
  lock(inside, false);

  // A locked artboard takes nothing in and lets nothing out.
  lock(board_a, true);
  CHECK(moves(loose, 0, -250).empty());
  CHECK(moves(inside, 450, 0).empty());
  CHECK(moves(inside, 0, 300).empty());
  lock(board_a, false);
  lock(board_b, true);
  CHECK(moves(inside, 450, 0).empty());
  CHECK(moves(inside, 0, 300) == (std::vector<Move>{{inside, std::nullopt}}));
  CHECK(moves(loose, 0, -250) == (std::vector<Move>{{loose, board_a}}));

  // The lock does not pass from an artboard to its children.
  CHECK(!patchy::layer_prevents_auto_nesting(*document.find_layer(inside)));
  CHECK((patchy::layer_effective_lock_flags(document.layers(), board_b) & patchy::kLayerLockArtboardAutonest) ==
        patchy::kLayerLockNone);
}

void artboard_rect_flatten_matches_the_full_flatten() {
  const auto document = clip_scene();
  const auto full = patchy::flatten_document_rgba8(document);
  for (const auto rect : {Rect{0, 0, 750, 380}, Rect{180, 40, 400, 300}, Rect{440, 0, 20, 380},
                          Rect{700, 360, 200, 200}, Rect{0, 125, 750, 1}}) {
    const auto clipped = patchy::intersect_rect(rect, Rect{0, 0, 750, 380});
    const auto part = patchy::flatten_document_rgba8_rect(document, rect);
    CHECK(part.width() == clipped.width && part.height() == clipped.height);
    bool same = part.width() == clipped.width && part.height() == clipped.height;
    for (std::int32_t y = 0; same && y < clipped.height; ++y) {
      for (std::int32_t x = 0; same && x < clipped.width; ++x) {
        same = std::equal(part.pixel(x, y), part.pixel(x, y) + 4, full.pixel(clipped.x + x, clipped.y + y));
      }
    }
    CHECK(same);
  }
}

}  // namespace

std::vector<patchy::test::TestCase> artboard_tests() {
  return {
      {"artboard_rect_flatten_matches_the_full_flatten", artboard_rect_flatten_matches_the_full_flatten},
      {"artboard_nest_moves_honor_prevent_auto_nesting", artboard_nest_moves_honor_prevent_auto_nesting},
      {"artboard_model_names_order_and_lookup", artboard_model_names_order_and_lookup},
      {"artboard_canvas_limits_background_and_masked_conversion",
       artboard_canvas_limits_background_and_masked_conversion},
      {"artboard_put_layers_on_new_artboard_keeps_order_and_refuses",
       artboard_put_layers_on_new_artboard_keeps_order_and_refuses},
      {"artboard_layer_tree_keeps_artboards_top_level", artboard_layer_tree_keeps_artboards_top_level},
      {"artboard_rects_follow_canvas_geometry", artboard_rects_follow_canvas_geometry},
      {"artboard_fit_and_grow_canvas_move_content_and_guides", artboard_fit_and_grow_canvas_move_content_and_guides},
      {"artboard_compositor_clips_children_and_paints_backgrounds",
       artboard_compositor_clips_children_and_paints_backgrounds},
      {"artboard_plain_group_conversion_composites_the_same", artboard_plain_group_conversion_composites_the_same},
      {"artboard_photoshop_fixtures_render_like_photoshop", artboard_photoshop_fixtures_render_like_photoshop},
      {"psd_artboard_blocks_regenerate_photoshop_bytes", psd_artboard_blocks_regenerate_photoshop_bytes},
      {"psd_artboard_round_trip_and_edits", psd_artboard_round_trip_and_edits},
      {"psd_artboard_local_fixtures_open_if_available", psd_artboard_local_fixtures_open_if_available},
  };
}
