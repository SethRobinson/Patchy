#include "core/layer_tree.hpp"

#include "core/layer_metadata.hpp"

#include <algorithm>
#include <iterator>
#include <array>
#include <functional>
#include <set>
#include <utility>

namespace patchy {

namespace {

bool target_is_inside_moved_layers(const std::vector<Layer>& layers, const std::vector<LayerId>& moving_ids,
                                   LayerId target_id) {
  for (const auto id : moving_ids) {
    const auto* moving = find_layer_in_tree(layers, id);
    if (moving != nullptr && (moving->id() == target_id || layer_contains_descendant(*moving, target_id))) {
      return true;
    }
  }
  return false;
}

void insert_layers_bottom_to_top(std::vector<Layer>& siblings, std::size_t insert_index,
                                 std::vector<Layer>& moved_top_to_bottom) {
  insert_index = std::min(insert_index, siblings.size());
  auto destination = siblings.begin() + static_cast<std::ptrdiff_t>(insert_index);
  for (auto it = moved_top_to_bottom.rbegin(); it != moved_top_to_bottom.rend(); ++it) {
    destination = siblings.insert(destination, std::move(*it));
    ++destination;
  }
}

bool layer_is_pixel_or_text_candidate(const Layer& layer) {
  return layer.kind() == LayerKind::Pixel || layer.kind() == LayerKind::Text || layer_is_text(layer);
}

using DefaultLayerCandidates = std::array<std::optional<LayerId>, 4>;

void collect_default_layer_candidates(const std::vector<Layer>& layers, bool ancestors_visible,
                                      LayerLockFlags ancestor_lock_flags, DefaultLayerCandidates& candidates) {
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    const auto effectively_visible = ancestors_visible && it->visible();
    const auto effective_lock_flags = ancestor_lock_flags | layer_lock_flags(*it);
    const auto pixels_locked = (effective_lock_flags & kLayerLockImagePixels) != kLayerLockNone;
    if (layer_is_pixel_or_text_candidate(*it)) {
      if (effectively_visible && !pixels_locked && !candidates[0].has_value()) {
        candidates[0] = it->id();
      }
      if (!candidates[2].has_value()) {
        candidates[2] = it->id();
      }
    } else if (it->kind() == LayerKind::Adjustment) {
      if (effectively_visible && !pixels_locked && !candidates[1].has_value()) {
        candidates[1] = it->id();
      }
      if (!candidates[3].has_value()) {
        candidates[3] = it->id();
      }
    }
    if (it->kind() == LayerKind::Group) {
      collect_default_layer_candidates(it->children(), effectively_visible, effective_lock_flags, candidates);
    }
  }
}

}  // namespace

std::size_t layer_descendant_count(const Layer& layer) {
  std::size_t count = layer.children().size();
  for (const auto& child : layer.children()) {
    count += layer_descendant_count(child);
  }
  return count;
}

void collect_layer_descendant_ids(const Layer& layer, std::vector<LayerId>& ids) {
  for (const auto& child : layer.children()) {
    ids.push_back(child.id());
    collect_layer_descendant_ids(child, ids);
  }
}

std::size_t layer_tree_count(const std::vector<Layer>& layers) {
  std::size_t count = layers.size();
  for (const auto& layer : layers) {
    count += layer_tree_count(layer.children());
  }
  return count;
}

std::optional<LayerId> default_non_group_layer_id(const std::vector<Layer>& layers) {
  DefaultLayerCandidates candidates{};
  collect_default_layer_candidates(layers, true, kLayerLockNone, candidates);
  for (const auto candidate : candidates) {
    if (candidate.has_value()) {
      return candidate;
    }
  }
  return std::nullopt;
}

std::optional<LayerId> only_layer_id(const std::vector<Layer>& layers) {
  if (layers.size() != 1U || !layers.front().children().empty()) {
    return std::nullopt;
  }
  return layers.front().id();
}

void collect_layer_group_ids(const std::vector<Layer>& layers, std::set<LayerId>& ids) {
  for (const auto& layer : layers) {
    if (layer.kind() == LayerKind::Group) {
      ids.insert(layer.id());
    }
    collect_layer_group_ids(layer.children(), ids);
  }
}

void collect_initially_collapsed_layer_groups(const std::vector<Layer>& layers, std::set<LayerId>& ids) {
  for (const auto& layer : layers) {
    if (layer.kind() == LayerKind::Group && !layer_group_expanded(layer)) {
      ids.insert(layer.id());
    }
    collect_initially_collapsed_layer_groups(layer.children(), ids);
  }
}

namespace {

void collect_layer_group_disclosure_changes(const std::vector<Layer>& layers, const std::set<LayerId>& collapsed_ids,
                                            std::vector<std::pair<LayerId, bool>>& changes) {
  for (const auto& layer : layers) {
    if (layer.kind() == LayerKind::Group) {
      const bool expanded = !collapsed_ids.contains(layer.id());
      if (layer_group_expanded(layer) != expanded) {
        changes.emplace_back(layer.id(), expanded);
      }
    }
    collect_layer_group_disclosure_changes(layer.children(), collapsed_ids, changes);
  }
}

}  // namespace

void apply_layer_group_disclosure(std::vector<Layer>& layers, const std::set<LayerId>& collapsed_ids) {
  std::vector<std::pair<LayerId, bool>> changes;
  collect_layer_group_disclosure_changes(std::as_const(layers), collapsed_ids, changes);
  for (const auto& [id, expanded] : changes) {
    if (auto* group = find_layer_in_tree(layers, id); group != nullptr) {
      group->metadata_without_content_bump()[kLayerMetadataGroupExpanded] = expanded ? "true" : "false";
    }
  }
}

void collect_initial_layer_fx_disclosure(const std::vector<Layer>& layers, std::set<LayerId>& expanded_effects,
                                         std::set<LayerId>& collapsed_smart_filters) {
  for (const auto& layer : layers) {
    if (const auto expanded = layer_fx_expanded(layer); expanded.has_value()) {
      if (*expanded) {
        expanded_effects.insert(layer.id());
      } else if (layer.smart_filter_stack() != nullptr) {
        collapsed_smart_filters.insert(layer.id());
      }
    }
    collect_initial_layer_fx_disclosure(layer.children(), expanded_effects, collapsed_smart_filters);
  }
}

namespace {

void collect_layer_fx_disclosure_changes(const std::vector<Layer>& layers, const std::set<LayerId>& expanded_effects,
                                         const std::set<LayerId>& collapsed_smart_filters,
                                         std::vector<std::pair<LayerId, bool>>& changes) {
  for (const auto& layer : layers) {
    const bool has_effects = layer.layer_style().effect_count() > 0;
    const bool has_smart_filters = layer.smart_filter_stack() != nullptr;
    if (has_effects || has_smart_filters) {
      const bool expanded = (has_effects && expanded_effects.contains(layer.id())) ||
                            (has_smart_filters && !collapsed_smart_filters.contains(layer.id()));
      if (layer_fx_expanded(layer) != std::optional<bool>{expanded}) {
        changes.emplace_back(layer.id(), expanded);
      }
    }
    collect_layer_fx_disclosure_changes(layer.children(), expanded_effects, collapsed_smart_filters, changes);
  }
}

}  // namespace

void apply_layer_fx_disclosure(std::vector<Layer>& layers, const std::set<LayerId>& expanded_effects,
                               const std::set<LayerId>& collapsed_smart_filters) {
  std::vector<std::pair<LayerId, bool>> changes;
  collect_layer_fx_disclosure_changes(std::as_const(layers), expanded_effects, collapsed_smart_filters, changes);
  for (const auto& [id, expanded] : changes) {
    if (auto* layer = find_layer_in_tree(layers, id); layer != nullptr) {
      layer->metadata_without_content_bump()[kLayerMetadataFxExpanded] = expanded ? "true" : "false";
    }
  }
}

bool collect_layer_ancestor_groups(const std::vector<Layer>& layers, LayerId id, std::vector<LayerId>& ancestors) {
  for (const auto& layer : layers) {
    if (layer.id() == id) {
      return true;
    }
    if (collect_layer_ancestor_groups(layer.children(), id, ancestors)) {
      if (layer.kind() == LayerKind::Group) {
        ancestors.push_back(layer.id());
      }
      return true;
    }
  }
  return false;
}

std::string_view sample_layers_token(SampleLayers mode) noexcept {
  switch (mode) {
  case SampleLayers::CurrentLayer:
    return "current";
  case SampleLayers::CurrentAndBelow:
    return "currentAndBelow";
  case SampleLayers::AllLayers:
    return "all";
  case SampleLayers::AllLayersNoAdjustments:
    return "allNoAdjustments";
  case SampleLayers::CurrentAndBelowNoAdjustments:
    return "currentAndBelowNoAdjustments";
  }
  return "all";
}

SampleLayers sample_layers_from_settings(std::optional<std::string_view> token,
                                         std::optional<bool> legacy_sample_all_layers) noexcept {
  if (token.has_value()) {
    for (const auto mode : {SampleLayers::CurrentLayer, SampleLayers::CurrentAndBelow, SampleLayers::AllLayers,
                            SampleLayers::AllLayersNoAdjustments, SampleLayers::CurrentAndBelowNoAdjustments}) {
      if (*token == sample_layers_token(mode)) {
        return mode;
      }
    }
  }
  if (legacy_sample_all_layers.has_value() && !*legacy_sample_all_layers) {
    return SampleLayers::CurrentLayer;
  }
  return SampleLayers::AllLayers;
}

namespace {

// Appends the later siblings at each level of the path to `id`; true once found.
bool collect_layers_above(const std::vector<Layer>& siblings, LayerId id, std::vector<LayerId>& above) {
  for (std::size_t index = 0; index < siblings.size(); ++index) {
    const auto& layer = siblings[index];
    if (layer.id() != id && !collect_layers_above(layer.children(), id, above)) {
      continue;
    }
    for (auto later = index + 1; later < siblings.size(); ++later) {
      above.push_back(siblings[later].id());
    }
    return true;
  }
  return false;
}

void collect_adjustment_layer_ids(const std::vector<Layer>& layers, std::vector<LayerId>& ids) {
  for (const auto& layer : layers) {
    if (layer.kind() == LayerKind::Adjustment) {
      ids.push_back(layer.id());
    }
    collect_adjustment_layer_ids(layer.children(), ids);
  }
}

}  // namespace

std::optional<std::vector<LayerId>> sample_layers_excluded_ids(const std::vector<Layer>& layers,
                                                               std::optional<LayerId> active_id, SampleLayers mode) {
  std::vector<LayerId> excluded;
  switch (mode) {
  case SampleLayers::CurrentLayer:
    return std::nullopt;
  case SampleLayers::AllLayers:
    return excluded;
  case SampleLayers::AllLayersNoAdjustments:
    collect_adjustment_layer_ids(layers, excluded);
    return excluded;
  case SampleLayers::CurrentAndBelow:
  case SampleLayers::CurrentAndBelowNoAdjustments:
    if (!active_id.has_value() || !collect_layers_above(layers, *active_id, excluded)) {
      return std::nullopt;
    }
    if (mode == SampleLayers::CurrentAndBelowNoAdjustments) {
      collect_adjustment_layer_ids(layers, excluded);
    }
    return excluded;
  }
  return std::nullopt;
}

const Layer* find_layer_in_tree(const std::vector<Layer>& layers, LayerId id) {
  for (const auto& layer : layers) {
    if (layer.id() == id) {
      return &layer;
    }
    if (const auto* found = find_layer_in_tree(layer.children(), id); found != nullptr) {
      return found;
    }
  }
  return nullptr;
}

Layer* find_layer_in_tree(std::vector<Layer>& layers, LayerId id) {
  // A lookup must not count as a mutation: descending through the non-const
  // children() accessor bumps every visited group's revisions and silently
  // invalidates the revision-keyed caches (see Document::find_layer_recursive).
  // Walk const, cast the hit.
  return const_cast<Layer*>(find_layer_in_tree(std::as_const(layers), id));
}

bool layer_contains_descendant(const Layer& layer, LayerId id) {
  for (const auto& child : layer.children()) {
    if (child.id() == id || layer_contains_descendant(child, id)) {
      return true;
    }
  }
  return false;
}

std::vector<LayerId> root_drop_layer_ids(const std::vector<Layer>& layers,
                                         const std::vector<LayerId>& ids_top_to_bottom) {
  std::vector<LayerId> roots;
  std::set<LayerId> selected(ids_top_to_bottom.begin(), ids_top_to_bottom.end());
  selected.erase(0);
  std::set<LayerId> found;
  std::set<LayerId> eligible;
  // One tree walk replaces a tree search for every pair of selected layers.
  // Preserve the caller's order below, including the all-or-nothing handling
  // of missing ids used by drag/drop and transform commands.
  const auto visit = [&](const auto& self, const std::vector<Layer>& siblings, bool ancestor_selected) -> void {
    for (const auto& layer : siblings) {
      const bool chosen = selected.contains(layer.id());
      if (chosen) {
        found.insert(layer.id());
        if (!ancestor_selected) { eligible.insert(layer.id()); }
      }
      self(self, layer.children(), ancestor_selected || chosen);
    }
  };
  visit(visit, layers, false);
  if (found.size() != selected.size()) { return {}; }
  for (const auto id : ids_top_to_bottom) {
    if (eligible.erase(id) != 0) { roots.push_back(id); }
  }
  return roots;
}

std::optional<Layer> take_layer_from_tree(std::vector<Layer>& layers, LayerId id) {
  const auto found = std::find_if(layers.begin(), layers.end(), [id](const Layer& layer) {
    return layer.id() == id;
  });
  if (found != layers.end()) {
    auto layer = std::move(*found);
    layers.erase(found);
    return layer;
  }

  for (auto& layer : layers) {
    if (auto child = take_layer_from_tree(layer.children(), id); child.has_value()) {
      return child;
    }
  }
  return std::nullopt;
}

std::optional<LayerSiblingLocation> find_layer_location(std::vector<Layer>& layers, LayerId id) {
  // Walk const, cast the hit (see find_layer_in_tree): the non-const descent
  // bumped every traversed group's revisions on every layer drag-drop.
  if (const auto found = find_layer_location(std::as_const(layers), id); found.has_value()) {
    return LayerSiblingLocation{const_cast<std::vector<Layer>*>(found->siblings), found->index};
  }
  return std::nullopt;
}

std::optional<ConstLayerSiblingLocation> find_layer_location(const std::vector<Layer>& layers, LayerId id) {
  for (std::size_t index = 0; index < layers.size(); ++index) {
    if (layers[index].id() == id) {
      return ConstLayerSiblingLocation{&layers, index};
    }
    const auto& children = layers[index].children();
    if (auto found = find_layer_location(children, id); found.has_value()) {
      return found;
    }
  }
  return std::nullopt;
}

const Layer* effective_clip_base(const std::vector<Layer>& siblings, std::size_t index) {
  if (index >= siblings.size()) {
    return nullptr;
  }
  while (index > 0) {
    const Layer& candidate = siblings[index - 1];
    if (candidate.clipped() && candidate.kind() != LayerKind::Group) {
      // Part of the same clipping run: keep walking toward its base.
      --index;
      continue;
    }
    return candidate.kind() == LayerKind::Pixel || candidate.kind() == LayerKind::Group ? &candidate : nullptr;
  }
  return nullptr;
}

std::vector<std::pair<LayerId, LayerId>> layer_tree_signature(const std::vector<Layer>& layers, LayerId parent_id) {
  std::vector<std::pair<LayerId, LayerId>> signature;
  std::function<void(const std::vector<Layer>&, LayerId)> append =
      [&](const std::vector<Layer>& current_layers, LayerId current_parent_id) {
    for (const auto& layer : current_layers) {
      signature.emplace_back(layer.id(), current_parent_id);
      append(layer.children(), layer.id());
    }
  };
  append(layers, parent_id);
  return signature;
}

bool move_layers_for_drop(std::vector<Layer>& layers, const LayerDropRequest& request) {
  auto moving_ids = root_drop_layer_ids(layers, request.layer_ids_top_to_bottom);
  if (moving_ids.empty()) {
    return false;
  }
  if (request.target_layer_id.has_value() &&
      target_is_inside_moved_layers(layers, moving_ids, *request.target_layer_id)) {
    return false;
  }
  // Artboards never nest (docs/artboards.md): one can only move among the top-level layers.
  const bool moving_artboard = std::any_of(moving_ids.begin(), moving_ids.end(), [&layers](LayerId id) {
    const auto* layer = find_layer_in_tree(std::as_const(layers), id);
    return layer != nullptr && layer->kind() == LayerKind::Group && layer->artboard().has_value();
  });
  if (moving_artboard && request.target_layer_id.has_value()) {
    const auto* target = find_layer_in_tree(std::as_const(layers), *request.target_layer_id);
    if (target == nullptr || (request.position == LayerDropPosition::OnItem && target->kind() == LayerKind::Group)) {
      return false;
    }
    const auto location = find_layer_location(std::as_const(layers), *request.target_layer_id);
    if (!location.has_value() || location->siblings != &std::as_const(layers)) {
      return false;
    }
  }

  std::vector<Layer> moved_top_to_bottom;
  moved_top_to_bottom.reserve(moving_ids.size());
  for (const auto id : moving_ids) {
    auto moved = take_layer_from_tree(layers, id);
    if (!moved.has_value()) {
      return false;
    }
    moved_top_to_bottom.push_back(std::move(*moved));
  }

  if (!request.target_layer_id.has_value()) {
    insert_layers_bottom_to_top(layers, 0, moved_top_to_bottom);
    return true;
  }

  if (request.position == LayerDropPosition::OnItem) {
    if (auto* target = find_layer_in_tree(layers, *request.target_layer_id);
        target != nullptr && target->kind() == LayerKind::Group) {
      insert_layers_bottom_to_top(target->children(), target->children().size(), moved_top_to_bottom);
      return true;
    }
  }

  auto target_location = find_layer_location(layers, *request.target_layer_id);
  if (!target_location.has_value() || target_location->siblings == nullptr) {
    return false;
  }

  const auto insert_index =
      request.position == LayerDropPosition::BelowItem ? target_location->index : target_location->index + 1U;
  insert_layers_bottom_to_top(*target_location->siblings, insert_index, moved_top_to_bottom);
  return true;
}

std::optional<std::vector<LayerId>> ungroup_layer(std::vector<Layer>& layers, LayerId group_id) {
  const auto location = find_layer_location(layers, group_id);
  if (!location.has_value() || location->siblings == nullptr) {
    return std::nullopt;
  }
  auto& siblings = *location->siblings;
  auto& group = siblings[location->index];
  if (group.kind() != LayerKind::Group) {
    return std::nullopt;
  }
  std::vector<Layer> children = std::move(group.children());
  group.children().clear();
  std::vector<LayerId> released;
  released.reserve(children.size());
  for (auto it = children.rbegin(); it != children.rend(); ++it) {
    released.push_back(it->id());
  }
  const auto index = static_cast<std::ptrdiff_t>(location->index);
  siblings.erase(siblings.begin() + index);
  siblings.insert(siblings.begin() + index, std::make_move_iterator(children.begin()),
                  std::make_move_iterator(children.end()));
  return released;
}

}  // namespace patchy
