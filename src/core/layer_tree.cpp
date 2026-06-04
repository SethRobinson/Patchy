#include "core/layer_tree.hpp"

#include "core/layer_metadata.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <set>

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
  for (auto& layer : layers) {
    if (layer.id() == id) {
      return &layer;
    }
    if (auto* found = find_layer_in_tree(layer.children(), id); found != nullptr) {
      return found;
    }
  }
  return nullptr;
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
  std::set<LayerId> seen;
  for (const auto id : ids_top_to_bottom) {
    if (id == 0 || seen.contains(id)) {
      continue;
    }
    const auto* layer = find_layer_in_tree(layers, id);
    if (layer == nullptr) {
      return {};
    }

    bool has_selected_ancestor = false;
    for (const auto possible_ancestor_id : ids_top_to_bottom) {
      if (possible_ancestor_id == id || possible_ancestor_id == 0) {
        continue;
      }
      const auto* possible_ancestor = find_layer_in_tree(layers, possible_ancestor_id);
      if (possible_ancestor != nullptr && layer_contains_descendant(*possible_ancestor, id)) {
        has_selected_ancestor = true;
        break;
      }
    }

    if (!has_selected_ancestor) {
      roots.push_back(id);
      seen.insert(id);
    }
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
  for (std::size_t index = 0; index < layers.size(); ++index) {
    if (layers[index].id() == id) {
      return LayerSiblingLocation{&layers, index};
    }
    if (auto found = find_layer_location(layers[index].children(), id); found.has_value()) {
      return found;
    }
  }
  return std::nullopt;
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

}  // namespace patchy
