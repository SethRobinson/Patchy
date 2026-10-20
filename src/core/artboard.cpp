#include "core/artboard.hpp"

#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/rect_utils.hpp"
#include "core/pixel_tools.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"

#include <algorithm>
#include <charconv>
#include <functional>
#include <set>
#include <string_view>

namespace patchy {
namespace {

Rect united(const std::optional<Rect>& a, Rect b) {
  if (!a.has_value() || a->empty()) {
    return b;
  }
  if (b.empty()) {
    return *a;
  }
  const auto left = std::min(a->x, b.x);
  const auto top = std::min(a->y, b.y);
  const auto right = std::max(a->x + a->width, b.x + b.width);
  const auto bottom = std::max(a->y + a->height, b.y + b.height);
  return Rect{left, top, right - left, bottom - top};
}

// Visible leaf bounds of a loose (non-artboard) top-level layer tree: what Photoshop's
// shrinkwrap keeps on the canvas. Adjustment layers have no extent of their own.
void unite_loose_bounds(const Layer& layer, std::optional<Rect>& extent) {
  if (!layer.visible()) {
    return;
  }
  if (layer.kind() == LayerKind::Group) {
    for (const auto& child : layer.children()) {
      unite_loose_bounds(child, extent);
    }
    return;
  }
  if (layer.kind() == LayerKind::Adjustment || layer.bounds().empty()) {
    return;
  }
  extent = united(extent, layer.bounds());
}

bool tree_contains(const Layer& layer, LayerId id) {
  if (layer.id() == id) {
    return true;
  }
  return std::any_of(layer.children().begin(), layer.children().end(),
                     [id](const Layer& child) { return tree_contains(child, id); });
}

std::optional<std::array<std::int32_t, 2>> resize_to(Document& document, Rect frame) {
  const auto canvas = Rect::from_size(document.width(), document.height());
  if (frame.empty() || frame == canvas) {
    return std::nullopt;
  }
  // Transparent extension, layers kept whole: a Background layer widens to the new canvas
  // (the canvas-resize rule) with transparency, so the new area stays empty.
  resize_canvas_to_frame(document, frame, EditColor{0, 0, 0, 0}, false);
  // Guides are document coordinates too (32nds of a pixel), and they belong to what they
  // were placed against.
  for (auto& guide : document.guides()) {
    guide.position_32 -= (guide.orientation == GuideOrientation::Vertical ? frame.x : frame.y) * 32;
  }
  return std::array<std::int32_t, 2>{-frame.x, -frame.y};
}

}  // namespace

bool layer_is_artboard(const Layer& layer) noexcept {
  return layer.kind() == LayerKind::Group && layer.artboard().has_value();
}

bool document_has_artboards(const Document& document) noexcept {
  return std::any_of(document.layers().begin(), document.layers().end(),
                     [](const Layer& layer) { return layer_is_artboard(layer); });
}

std::vector<const Layer*> document_artboards(const Document& document) {
  std::vector<const Layer*> artboards;
  for (auto it = document.layers().rbegin(); it != document.layers().rend(); ++it) {
    if (layer_is_artboard(*it)) {
      artboards.push_back(&*it);
    }
  }
  return artboards;
}

std::vector<Layer*> document_artboards(Document& document) {
  std::vector<Layer*> artboards;
  for (auto it = document.layers().rbegin(); it != document.layers().rend(); ++it) {
    if (layer_is_artboard(std::as_const(*it))) {
      artboards.push_back(&*it);
    }
  }
  return artboards;
}

const Layer* artboard_at(const Document& document, std::int32_t x, std::int32_t y) {
  for (const auto* artboard : document_artboards(document)) {
    if (artboard->artboard()->rect.contains(x, y)) {
      return artboard;
    }
  }
  return nullptr;
}

const Layer* artboard_owning_layer(const Document& document, LayerId id) {
  for (const auto& layer : document.layers()) {
    if (layer_is_artboard(layer) && tree_contains(layer, id)) {
      return &layer;
    }
  }
  return nullptr;
}

std::optional<LayerId> top_level_ancestor_id(const Document& document, LayerId id) {
  for (const auto& layer : document.layers()) {
    if (tree_contains(layer, id)) {
      return layer.id();
    }
  }
  return std::nullopt;
}

std::optional<Rect> artboard_union_bounds(const Document& document) {
  std::optional<Rect> extent;
  for (const auto& layer : document.layers()) {
    if (layer_is_artboard(layer)) {
      extent = united(extent, layer.artboard()->rect);
    }
  }
  return extent;
}

std::string next_artboard_name(const Document& document) {
  constexpr std::string_view kPrefix = "Artboard ";
  int highest = 0;
  for (const auto& layer : document.layers()) {
    const std::string_view name = layer.name();
    if (!name.starts_with(kPrefix)) {
      continue;
    }
    int number = 0;
    const auto digits = name.substr(kPrefix.size());
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
    if (error == std::errc() && end == digits.data() + digits.size()) {
      highest = std::max(highest, number);
    }
  }
  return std::string(kPrefix) + std::to_string(highest + 1);
}

std::vector<LayerId> artboard_nest_roots(const Document& document, const std::vector<LayerId>& selected_ids,
                                         const std::vector<LayerId>& moved_leaf_ids) {
  const auto& layers = document.layers();
  const auto holds_moved = [&](const Layer& root) {
    return std::any_of(moved_leaf_ids.begin(), moved_leaf_ids.end(),
                       [&](LayerId moved) { return tree_contains(root, moved); });
  };
  std::vector<LayerId> roots;
  for (const auto id : root_drop_layer_ids(layers, selected_ids)) {
    const auto* layer = document.find_layer(id);
    if (layer != nullptr && !layer_is_artboard(*layer) && holds_moved(*layer)) {
      roots.push_back(id);
    }
  }
  if (roots.empty()) {
    for (const auto id : root_drop_layer_ids(layers, moved_leaf_ids)) {
      const auto* layer = document.find_layer(id);
      if (layer != nullptr && !layer_is_artboard(*layer)) {
        roots.push_back(id);
      }
    }
  }
  return roots;
}

std::vector<ArtboardNestMove> artboard_nest_moves(const Document& document, const std::vector<LayerId>& root_ids,
                                                  std::int32_t dx, std::int32_t dy) {
  std::vector<ArtboardNestMove> moves;
  if (!document_has_artboards(document)) {
    return moves;
  }
  for (const auto id : root_ids) {
    const auto* layer = document.find_layer(id);
    if (layer == nullptr || layer_is_artboard(*layer)) {
      continue;
    }
    const auto bounds = layer_render_bounds(*layer);
    if (bounds.empty()) {
      continue;
    }
    const auto* target = artboard_at(document, bounds.x + dx + bounds.width / 2, bounds.y + dy + bounds.height / 2);
    const auto* owner = artboard_owning_layer(document, id);
    if (target == owner) {
      continue;
    }
    // Prevent Auto-Nesting (GitHub issue 246): a locked layer stays where it is in the
    // tree, and nothing auto-nests into or out of a locked artboard.
    if (layer_prevents_auto_nesting(*layer) || (owner != nullptr && layer_prevents_auto_nesting(*owner)) ||
        (target != nullptr && layer_prevents_auto_nesting(*target))) {
      continue;
    }
    moves.push_back({id, target != nullptr ? std::optional<LayerId>(target->id()) : std::nullopt});
  }
  return moves;
}

bool apply_artboard_nest_moves(Document& document, const std::vector<ArtboardNestMove>& moves) {
  bool changed = false;
  for (const auto& move : moves) {
    const auto* owner = artboard_owning_layer(std::as_const(document), move.id);
    const auto owner_id = owner != nullptr ? std::optional<LayerId>(owner->id()) : std::nullopt;
    if (owner_id == move.target) {
      continue;
    }
    Layer* board = nullptr;
    if (move.target.has_value()) {
      for (auto& layer : document.layers()) {
        if (layer.id() == *move.target && layer_is_artboard(std::as_const(layer))) {
          board = &layer;
        }
      }
      if (board == nullptr) {
        continue;
      }
    }
    auto taken = take_layer_from_tree(document.layers(), move.id);
    if (!taken.has_value()) {
      continue;
    }
    if (move.target.has_value()) {
      // take_layer_from_tree may have shifted top-level layers: find the board again.
      for (auto& layer : document.layers()) {
        if (layer.id() == *move.target) {
          layer.add_child(std::move(*taken));
        }
      }
    } else {
      auto insert_at = document.layers().end();
      for (auto it = document.layers().begin(); it != document.layers().end(); ++it) {
        if (owner_id.has_value() && it->id() == *owner_id) {
          insert_at = it + 1;
        }
      }
      document.layers().insert(insert_at, std::move(*taken));
    }
    changed = true;
  }
  return changed;
}

bool artboard_composites_plainly(const Layer& artboard) noexcept {
  return (artboard.blend_mode() == BlendMode::Normal || artboard.blend_mode() == BlendMode::PassThrough) &&
         artboard.opacity() >= 1.0F && artboard.fill_opacity() >= 1.0F &&
         (!artboard.mask().has_value() || artboard.mask()->disabled) && artboard.vector_mask() == nullptr &&
         artboard.layer_style().empty();
}

std::optional<LayerId> put_layers_on_new_artboard(Document& document, const std::vector<LayerId>& ids,
                                                  std::string name) {
  if (ids.empty()) {
    return std::nullopt;
  }
  const std::set<LayerId> wanted(ids.begin(), ids.end());
  // Bottom to top, the order Layer::children() keeps; a listed layer's subtree is not
  // searched, so a listed descendant shows up as missing.
  std::vector<LayerId> ordered;
  std::optional<Rect> bounds;
  bool valid = true;
  const std::function<void(const std::vector<Layer>&)> collect = [&](const std::vector<Layer>& layers) {
    for (const auto& layer : layers) {
      if (layer_is_artboard(layer)) {
        if (wanted.contains(layer.id())) {
          valid = false;
        }
        continue;  // nothing on an artboard qualifies
      }
      if (wanted.contains(layer.id())) {
        ordered.push_back(layer.id());
        if (const auto render = layer_render_bounds(layer); !render.empty()) {
          bounds = bounds.has_value() ? unite_rect(*bounds, render) : render;
        }
        continue;
      }
      collect(layer.children());
    }
  };
  collect(std::as_const(document).layers());
  if (!valid || ordered.size() != wanted.size()) {
    return std::nullopt;
  }
  if (name.empty()) {
    name = next_artboard_name(std::as_const(document));
  }
  auto artboard = make_artboard_group(document.allocate_layer_id(), std::move(name),
                                      bounds.value_or(Rect::from_size(document.width(), document.height())));
  for (const auto id : ordered) {
    if (auto taken = take_layer_from_tree(document.layers(), id); taken.has_value()) {
      artboard.add_child(std::move(*taken));
    }
  }
  const auto id = artboard.id();
  document.layers().insert(document.layers().begin() + static_cast<std::ptrdiff_t>(new_artboard_insert_index(document)),
                           std::move(artboard));
  return id;
}

Layer make_artboard_group(LayerId id, std::string name, Rect rect, ArtboardBackground background, RgbColor color) {
  Layer group(id, std::move(name), LayerKind::Group);
  // Photoshop makes artboards Normal, not Pass Through: the background and the children
  // composite in their own group before they meet what is below.
  group.set_blend_mode(BlendMode::Normal);
  LayerArtboard artboard;
  artboard.rect = rect;
  artboard.background = background;
  artboard.color = background == ArtboardBackground::Custom ? color : RgbColor{255, 255, 255};
  group.set_artboard(std::move(artboard));
  return group;
}

std::optional<std::array<std::uint8_t, 4>> artboard_background_rgba(const LayerArtboard& artboard) noexcept {
  switch (artboard.background) {
    case ArtboardBackground::White:
      return std::array<std::uint8_t, 4>{255, 255, 255, 255};
    case ArtboardBackground::Black:
      return std::array<std::uint8_t, 4>{0, 0, 0, 255};
    case ArtboardBackground::Custom:
      return std::array<std::uint8_t, 4>{artboard.color.red, artboard.color.green, artboard.color.blue, 255};
    case ArtboardBackground::Transparent:
      break;
  }
  return std::nullopt;
}

std::optional<Rect> artboard_document_extent(const Document& document) {
  std::optional<Rect> extent = artboard_union_bounds(document);
  if (!extent.has_value()) {
    return std::nullopt;
  }
  const auto& layers = document.layers();
  for (std::size_t index = 0; index < layers.size(); ++index) {
    const auto& layer = layers[index];
    // The bottom "Background" widens with every canvas growth (the canvas-resize rule),
    // so counting it would keep the canvas from ever shrinking back.
    if (layer_is_artboard(layer) || (index == 0U && layer.name() == "Background")) {
      continue;
    }
    unite_loose_bounds(layer, extent);
  }
  return extent;
}

namespace {

struct WideRect {
  std::int64_t left;
  std::int64_t top;
  std::int64_t right;
  std::int64_t bottom;
};

std::optional<WideRect> grown_canvas(const Document& document) {
  WideRect canvas{0, 0, document.width(), document.height()};
  bool any = false;
  for (const auto& layer : document.layers()) {
    if (!layer_is_artboard(layer)) {
      continue;
    }
    const auto& rect = layer.artboard()->rect;
    any = true;
    canvas.left = std::min<std::int64_t>(canvas.left, rect.x);
    canvas.top = std::min<std::int64_t>(canvas.top, rect.y);
    canvas.right = std::max<std::int64_t>(canvas.right, static_cast<std::int64_t>(rect.x) + rect.width);
    canvas.bottom = std::max<std::int64_t>(canvas.bottom, static_cast<std::int64_t>(rect.y) + rect.height);
  }
  return any ? std::optional<WideRect>(canvas) : std::nullopt;
}

}  // namespace

bool artboard_canvas_within_limit(const Document& document) {
  const auto canvas = grown_canvas(document);
  return !canvas.has_value() || (canvas->right - canvas->left <= kMaxArtboardCanvasSide &&
                                 canvas->bottom - canvas->top <= kMaxArtboardCanvasSide);
}

bool artboard_canvas_would_grow(const Document& document) {
  const auto canvas = grown_canvas(document);
  return canvas.has_value() && (canvas->left < 0 || canvas->top < 0 || canvas->right > document.width() ||
                                canvas->bottom > document.height());
}

std::optional<std::array<std::int32_t, 2>> fit_canvas_to_artboards(Document& document) {
  const auto extent = artboard_document_extent(document);
  if (!extent.has_value()) {
    return std::nullopt;
  }
  return resize_to(document, *extent);
}

std::optional<std::array<std::int32_t, 2>> grow_canvas_to_artboards(Document& document) {
  const auto artboards = artboard_union_bounds(document);
  if (!artboards.has_value() || !artboard_canvas_within_limit(document)) {
    return std::nullopt;
  }
  return resize_to(document, united(Rect::from_size(document.width(), document.height()), *artboards));
}

std::size_t new_artboard_insert_index(const Document& document) noexcept {
  return document.layers().size();
}

Rect artboard_rect_beside(const Document& document, std::int32_t width, std::int32_t height) {
  const Layer* rightmost = nullptr;
  for (const auto* artboard : document_artboards(document)) {
    const auto& rect = artboard->artboard()->rect;
    if (rightmost == nullptr || rect.x + rect.width > rightmost->artboard()->rect.x + rightmost->artboard()->rect.width) {
      rightmost = artboard;
    }
  }
  if (rightmost == nullptr) {
    return Rect{0, 0, width, height};
  }
  return artboard_rect_next_to(rightmost->artboard()->rect, 0, width, height);
}

Rect artboard_rect_next_to(Rect anchor, int side, std::int32_t width, std::int32_t height) noexcept {
  switch (side) {
    case 1:
      return Rect{anchor.x, anchor.y + anchor.height + kArtboardSpacing, width, height};
    case 2:
      return Rect{anchor.x - kArtboardSpacing - width, anchor.y, width, height};
    case 3:
      return Rect{anchor.x, anchor.y - kArtboardSpacing - height, width, height};
    default:
      return Rect{anchor.x + anchor.width + kArtboardSpacing, anchor.y, width, height};
  }
}

namespace {

VectorPath rect_path(Rect rect) {
  PathSubpath subpath;
  for (const auto& [x, y] : {std::pair{rect.x, rect.y}, std::pair{rect.x + rect.width, rect.y},
                             std::pair{rect.x + rect.width, rect.y + rect.height},
                             std::pair{rect.x, rect.y + rect.height}}) {
    PathAnchor anchor;
    anchor.anchor_x = anchor.in_x = anchor.out_x = static_cast<double>(x);
    anchor.anchor_y = anchor.in_y = anchor.out_y = static_cast<double>(y);
    subpath.anchors.push_back(anchor);
  }
  subpath.closed = true;
  VectorPath path;
  path.subpaths.push_back(std::move(subpath));
  return path;
}

}  // namespace

void convert_artboards_to_plain_groups(Document& document) {
  const auto canvas = Rect::from_size(document.width(), document.height());
  for (auto& layer : document.layers()) {
    if (!layer_is_artboard(std::as_const(layer))) {
      continue;
    }
    const auto artboard = *std::as_const(layer).artboard();
    layer.clear_artboard();
    if (layer.blend_mode() == BlendMode::PassThrough) {
      // The artboard's paper painted under its children inside the group only when
      // it was isolated; a Pass Through artboard is drawn that way too.
      layer.set_blend_mode(BlendMode::Normal);
    }
    if (const auto paper = artboard_background_rgba(artboard); paper.has_value()) {
      VectorShapeContent content;
      content.path = rect_path(artboard.rect);
      content.fill.kind = VectorFillKind::Solid;
      content.fill.color = RgbColor{(*paper)[0], (*paper)[1], (*paper)[2]};
      Layer background(document.allocate_layer_id(), layer.name() + " Background", LayerKind::Pixel);
      background.metadata()[kLayerMetadataVectorShape] = "1";
      mark_layer_vector_block_dirty(background);
      background.set_vector_shape(std::move(content));
      update_vector_shape_raster(background, canvas, &document.metadata().patterns);
      layer.children().insert(layer.children().begin(), std::move(background));
    }
    LayerVectorMask mask;
    mask.path = rect_path(artboard.rect);
    if (std::as_const(layer).vector_mask() == nullptr) {
      layer.set_vector_mask(std::move(mask));
      update_vector_mask_raster(layer, canvas);
      continue;
    }
    // The group keeps its own vector mask: the rect clip goes on an inner group holding
    // the children (an isolated group inside an isolated group composites the same).
    Layer inner(document.allocate_layer_id(), layer.name() + " Clip", LayerKind::Group);
    inner.set_blend_mode(BlendMode::Normal);
    inner.set_vector_mask(std::move(mask));
    update_vector_mask_raster(inner, canvas);
    for (auto& child : layer.children()) {
      inner.add_child(std::move(child));
    }
    layer.children().clear();
    layer.add_child(std::move(inner));
  }
}

}  // namespace patchy
