// GitHub issue 755: which pattern references a document cannot answer itself, and what a
// save does with the placeholder an earlier save wrote under such an id.
#include "core/document.hpp"
#include "core/pattern_resource.hpp"
#include "core/vector_shape.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_patterns.hpp"
#include "render/compositor.hpp"
#include "core_test_support.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char* kOverlayId = "75500000-1111-4222-8333-000000000001";
constexpr const char* kTextureId = "75500000-1111-4222-8333-000000000002";
constexpr const char* kFillId = "75500000-1111-4222-8333-000000000003";
constexpr const char* kPartId = "75500000-1111-4222-8333-000000000004";
constexpr const char* kHeldId = "75500000-1111-4222-8333-000000000005";

patchy::PixelBuffer checker_tile(std::uint8_t dark, std::uint8_t light) {
  patchy::PixelBuffer tile(4, 4, patchy::PixelFormat::rgba8());
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      auto* px = tile.pixel(x, y);
      const auto level = ((x / 2 + y / 2) % 2 == 0) ? dark : light;
      px[0] = level;
      px[1] = static_cast<std::uint8_t>(255 - level);
      px[2] = 40;
      px[3] = 255;
    }
  }
  return tile;
}

patchy::PatternResource resource(const char* id, const char* name, patchy::PixelBuffer tile) {
  patchy::PatternResource result;
  result.id = id;
  result.name = name;
  result.tile = std::move(tile);
  return result;
}

patchy::Layer styled_layer(patchy::Document& document, const char* overlay_id, const char* texture_id) {
  patchy::Layer layer(document.allocate_layer_id(), "Styled", patchy::test::solid_rgba(32, 32, 230, 60, 200, 255));
  patchy::LayerPatternOverlay overlay;
  overlay.enabled = true;
  overlay.pattern_id = overlay_id;
  overlay.pattern_name = "Overlay";
  layer.layer_style().pattern_overlays = {overlay};
  patchy::LayerBevelEmboss bevel;
  bevel.enabled = false;  // a switched-off effect still names its pattern
  bevel.texture.enabled = true;
  bevel.texture.pattern_id = texture_id;
  layer.layer_style().bevels = {bevel};
  return layer;
}

void unresolved_pattern_ids_lists_what_the_document_cannot_draw() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("bg", patchy::test::solid_rgba(64, 64, 255, 255, 255, 255));
  CHECK(patchy::unresolved_pattern_ids(std::as_const(document).layers(), document.metadata().patterns).empty());

  // A group holding a styled layer and a shape whose fill, and whose second part's stroke,
  // are pattern paints. The Solid stroke keeps a pattern id from before it was switched.
  patchy::Layer group(document.allocate_layer_id(), "Group", patchy::LayerKind::Group);
  group.children().push_back(styled_layer(document, kOverlayId, kTextureId));
  patchy::Layer shape(document.allocate_layer_id(), "Shape", patchy::PixelBuffer());
  patchy::VectorShapeContent content;
  content.fill.kind = patchy::VectorFillKind::Pattern;
  content.fill.pattern_id = kFillId;
  content.stroke.content.kind = patchy::VectorFillKind::Solid;
  content.stroke.content.pattern_id = "not-a-reference";
  patchy::VectorShapePart part;
  part.stroke.content.kind = patchy::VectorFillKind::Pattern;
  part.stroke.content.pattern_id = kPartId;
  content.parts.push_back(part);
  shape.set_vector_shape(std::move(content));
  group.children().push_back(std::move(shape));
  document.add_layer(std::move(group));
  document.add_layer(styled_layer(document, kHeldId, kOverlayId));  // the overlay id a second time

  auto& store = document.metadata().patterns;
  store.adopt(resource(kHeldId, "Held", checker_tile(20, 220)));
  const auto ids = [&] { return patchy::unresolved_pattern_ids(std::as_const(document).layers(), store); };
  CHECK(ids() == std::vector<std::string>({kOverlayId, kTextureId, kFillId, kPartId}));

  // The writer's placeholder and an empty tile answer nothing; a real tile does.
  store.adopt(resource(kOverlayId, "Placeholder", patchy::PixelBuffer(1, 1, patchy::PixelFormat::rgba8())));
  store.adopt(resource(kTextureId, "Empty", patchy::PixelBuffer()));
  CHECK(patchy::pattern_tile_is_unrenderable(store.find(kOverlayId)->tile));
  CHECK(ids().size() == 4U);
  store.adopt(resource(kOverlayId, "Overlay", checker_tile(10, 200)));
  store.adopt(resource(kFillId, "Fill", checker_tile(30, 180)));
  CHECK(ids() == std::vector<std::string>({kTextureId, kPartId}));

  const std::vector<std::string> fill_only{kFillId};
  const std::vector<std::string> part_only{kPartId};
  const std::vector<std::string> overlay_only{kOverlayId};
  const auto& saved_group = std::as_const(document).layers()[1];
  const auto& saved_shape = saved_group.children()[1];
  CHECK(patchy::vector_shape_paints_with_pattern(saved_shape, fill_only));
  CHECK(patchy::vector_shape_paints_with_pattern(saved_shape, part_only));
  CHECK(!patchy::vector_shape_paints_with_pattern(saved_shape, overlay_only));
  CHECK(!patchy::vector_shape_paints_with_pattern(saved_group.children()[0], fill_only));  // no shape
}

std::vector<std::vector<std::uint8_t>> pattern_blocks(const patchy::Document& document) {
  std::vector<std::vector<std::uint8_t>> blocks;
  for (const auto& block : document.metadata().unknown_psd_resources) {
    if (block.key == "Patt" || block.key == "Pat2" || block.key == "Pat3") {
      blocks.push_back(block.payload);
    }
  }
  return blocks;
}

void psd_placeholder_pattern_is_replaced_once_the_document_holds_the_tile() {
  // First save: the overlay's pattern is nowhere, so the file gets the 1x1 placeholder;
  // the held pattern is written as it is.
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("bg", patchy::test::solid_rgba(64, 64, 255, 255, 255, 255));
  document.add_layer(styled_layer(document, kOverlayId, kHeldId));
  const auto held_tile = checker_tile(20, 220);
  document.metadata().patterns.adopt(resource(kHeldId, "Held", held_tile));
  const auto first = patchy::psd::DocumentIo::write_layered_rgb8(document);

  auto reopened = patchy::psd::DocumentIo::read(first, {});
  const auto* placeholder = reopened.metadata().patterns.find(kOverlayId);
  CHECK(placeholder != nullptr && patchy::pattern_tile_is_unrenderable(placeholder->tile));
  const auto without_overlay = patchy::Compositor{}.flatten_rgb8(reopened);

  // Unchanged, a save keeps the block byte for byte.
  const auto first_blocks = pattern_blocks(reopened);
  CHECK(first_blocks.size() == 1U);
  CHECK(!patchy::psd::replace_placeholder_patterns(first_blocks.front(), reopened.metadata().patterns).has_value());
  CHECK(pattern_blocks(patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(reopened), {})) ==
        first_blocks);

  // The document gains the tile (the pattern library had it): the next save writes it in
  // the placeholder's place, once, and leaves the other record alone.
  const auto overlay_tile = checker_tile(10, 200);
  reopened.metadata().patterns.adopt(resource(kOverlayId, "Overlay", overlay_tile));
  const auto with_overlay = patchy::Compositor{}.flatten_rgb8(reopened);
  CHECK(!std::equal(with_overlay.data().begin(), with_overlay.data().end(), without_overlay.data().begin()));
  const auto second = patchy::psd::DocumentIo::write_layered_rgb8(reopened);
  const auto saved = patchy::psd::DocumentIo::read(second, {});
  const auto second_blocks = pattern_blocks(saved);
  CHECK(second_blocks.size() == 1U);
  const auto ids = patchy::psd::pattern_ids_in_block(second_blocks.front());
  CHECK(std::count(ids.begin(), ids.end(), std::string(kOverlayId)) == 1);
  CHECK(std::count(ids.begin(), ids.end(), std::string(kHeldId)) == 1);
  const auto same_tile = [](const patchy::PixelBuffer& a, const patchy::PixelBuffer& b) {
    return a.width() == b.width() && a.height() == b.height() &&
           std::equal(a.data().begin(), a.data().end(), b.data().begin());
  };
  const auto* healed = saved.metadata().patterns.find(kOverlayId);
  const auto* held = saved.metadata().patterns.find(kHeldId);
  CHECK(healed != nullptr && same_tile(healed->tile, overlay_tile));
  CHECK(healed != nullptr && healed->name == "Overlay");
  CHECK(held != nullptr && same_tile(held->tile, held_tile));
  const auto saved_render = patchy::Compositor{}.flatten_rgb8(saved);
  CHECK(std::equal(saved_render.data().begin(), saved_render.data().end(), with_overlay.data().begin()));
}

}  // namespace

std::vector<patchy::test::TestCase> pattern_resolution_tests() {
  return {
      {"unresolved_pattern_ids_lists_what_the_document_cannot_draw",
       unresolved_pattern_ids_lists_what_the_document_cannot_draw},
      {"psd_placeholder_pattern_is_replaced_once_the_document_holds_the_tile",
       psd_placeholder_pattern_is_replaced_once_the_document_holds_the_tile},
  };
}
