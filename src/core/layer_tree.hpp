#pragma once

#include "core/layer.hpp"

#include <cstddef>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace patchy {

enum class LayerDropPosition {
  OnItem,
  AboveItem,
  BelowItem,
  OnViewport
};

struct LayerDropRequest {
  std::vector<LayerId> layer_ids_top_to_bottom;
  std::optional<LayerId> target_layer_id;
  LayerDropPosition position{LayerDropPosition::OnViewport};
};

struct LayerSiblingLocation {
  std::vector<Layer>* siblings{nullptr};
  std::size_t index{0};
};

struct ConstLayerSiblingLocation {
  const std::vector<Layer>* siblings{nullptr};
  std::size_t index{0};
};

[[nodiscard]] std::size_t layer_descendant_count(const Layer& layer);
[[nodiscard]] std::size_t layer_tree_count(const std::vector<Layer>& layers);
[[nodiscard]] std::optional<LayerId> default_non_group_layer_id(const std::vector<Layer>& layers);
void collect_layer_group_ids(const std::vector<Layer>& layers, std::set<LayerId>& ids);
void collect_initially_collapsed_layer_groups(const std::vector<Layer>& layers, std::set<LayerId>& ids);
[[nodiscard]] bool collect_layer_ancestor_groups(const std::vector<Layer>& layers, LayerId id,
                                                 std::vector<LayerId>& ancestors);
[[nodiscard]] const Layer* find_layer_in_tree(const std::vector<Layer>& layers, LayerId id);
[[nodiscard]] Layer* find_layer_in_tree(std::vector<Layer>& layers, LayerId id);
[[nodiscard]] bool layer_contains_descendant(const Layer& layer, LayerId id);
[[nodiscard]] std::vector<LayerId> root_drop_layer_ids(const std::vector<Layer>& layers,
                                                       const std::vector<LayerId>& ids_top_to_bottom);
[[nodiscard]] std::optional<Layer> take_layer_from_tree(std::vector<Layer>& layers, LayerId id);
[[nodiscard]] std::optional<LayerSiblingLocation> find_layer_location(std::vector<Layer>& layers, LayerId id);
// Const walk (no revision bumps) for read-only callers.
[[nodiscard]] std::optional<ConstLayerSiblingLocation> find_layer_location(const std::vector<Layer>& layers,
                                                                           LayerId id);
// The layer a clipped sibling at `index` would clip to: walks down through the
// consecutive clipped run below it and returns the first non-clipped sibling if
// it can host a clipping group (a pixel layer), else nullptr. Index 0 (nothing
// below) and group/adjustment bases yield nullptr - such flags render unclipped.
[[nodiscard]] const Layer* effective_clip_base(const std::vector<Layer>& siblings, std::size_t index);
[[nodiscard]] std::vector<std::pair<LayerId, LayerId>> layer_tree_signature(const std::vector<Layer>& layers,
                                                                            LayerId parent_id = 0);
bool move_layers_for_drop(std::vector<Layer>& layers, const LayerDropRequest& request);

}  // namespace patchy
