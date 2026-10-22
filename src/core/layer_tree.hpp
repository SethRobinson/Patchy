#pragma once

#include "core/layer.hpp"

#include <cstddef>
#include <optional>
#include <set>
#include <string_view>
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
  // Alt-drop (Photoshop's Alt-drag): duplicate the dragged layers at the drop
  // position instead of moving them. move_layers_for_drop ignores it; the
  // panel host clones first and moves the clones.
  bool copy{false};
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
// Pre-order ids of every descendant; the layer's own id is not appended.
void collect_layer_descendant_ids(const Layer& layer, std::vector<LayerId>& ids);
[[nodiscard]] std::size_t layer_tree_count(const std::vector<Layer>& layers);
[[nodiscard]] std::optional<LayerId> default_non_group_layer_id(const std::vector<Layer>& layers);
// The id of the tree's single layer: exactly one root and nothing under it.
// A layer command in such a document has only one possible target.
[[nodiscard]] std::optional<LayerId> only_layer_id(const std::vector<Layer>& layers);
void collect_layer_group_ids(const std::vector<Layer>& layers, std::set<LayerId>& ids);
void collect_initially_collapsed_layer_groups(const std::vector<Layer>& layers, std::set<LayerId>& ids);
// Writes the Layers panel's open/closed folder state (`collapsed_ids`, the session's runtime
// source of truth) into each group's expand metadata, which every layered writer reads (PSD
// section divider type 1 open / 2 closed). Call it on the copy a save writes: only groups whose
// metadata disagrees are touched, and without a content revision bump (expand state never renders).
void apply_layer_group_disclosure(std::vector<Layer>& layers, const std::set<LayerId>& collapsed_ids);
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
// it can host a clipping group (a pixel layer or folder), else nullptr. Index 0
// (nothing below) and adjustment bases yield nullptr; such flags render unclipped.
[[nodiscard]] const Layer* effective_clip_base(const std::vector<Layer>& siblings, std::size_t index);
[[nodiscard]] std::vector<std::pair<LayerId, LayerId>> layer_tree_signature(const std::vector<Layer>& layers,
                                                                            LayerId parent_id = 0);
// The Pick tool's Sample choices (GitHub issue 321), in menu order.
enum class SampleLayers {
  CurrentLayer,
  CurrentAndBelow,
  AllLayers,
  AllLayersNoAdjustments,
  CurrentAndBelowNoAdjustments
};
// Settings tokens (`tools/eyedropperSampleLayers`); permanent identifiers.
[[nodiscard]] std::string_view sample_layers_token(SampleLayers mode) noexcept;
// The stored choice: a known token wins; otherwise the pre-321 bool
// (`tools/eyedropperSampleAllLayers`: true All Layers, false Current Layer); otherwise All Layers.
[[nodiscard]] SampleLayers sample_layers_from_settings(std::optional<std::string_view> token,
                                                       std::optional<bool> legacy_sample_all_layers) noexcept;
// The layers a composite Sample choice leaves out, for a hidden-layer render: for the
// Current & Below choices, everything above `active_id` in compositing order (the later
// siblings at each level of its ancestor path; a hidden group hides its subtree), and for
// the No Adjustments choices every adjustment layer. Empty for All Layers. nullopt when a
// Current & Below choice has no active layer in the tree. CurrentLayer is not a composite
// and yields nullopt.
[[nodiscard]] std::optional<std::vector<LayerId>> sample_layers_excluded_ids(const std::vector<Layer>& layers,
                                                                             std::optional<LayerId> active_id,
                                                                             SampleLayers mode);
bool move_layers_for_drop(std::vector<Layer>& layers, const LayerDropRequest& request);
// Ungroup: replaces the group with its children at the group's position (storage
// order kept, so composite order is unchanged). Returns the released ids top to
// bottom; nullopt when `group_id` is missing or not a group. The group's own
// opacity, blend mode, masks, and style are dropped with it (Photoshop's rule).
[[nodiscard]] std::optional<std::vector<LayerId>> ungroup_layer(std::vector<Layer>& layers, LayerId group_id);

}  // namespace patchy
