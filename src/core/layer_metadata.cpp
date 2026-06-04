#include "core/layer_metadata.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace patchy {

namespace {

bool has_all_flags(LayerLockFlags value, LayerLockFlags flags) {
  return flags != kLayerLockNone && (value & flags) == flags;
}

bool has_any_flags(LayerLockFlags value, LayerLockFlags flags) {
  return (value & flags) != kLayerLockNone;
}

}  // namespace

bool layer_locks_transparent_pixels(const Layer& layer) {
  return has_all_flags(layer.lock_flags(), kLayerLockTransparentPixels);
}

void set_layer_locks_transparent_pixels(Layer& layer, bool locked) {
  set_layer_lock_flag(layer, kLayerLockTransparentPixels, locked);
}

bool layer_locks_image_pixels(const Layer& layer) {
  return has_all_flags(layer.lock_flags(), kLayerLockImagePixels);
}

void set_layer_locks_image_pixels(Layer& layer, bool locked) {
  set_layer_lock_flag(layer, kLayerLockImagePixels, locked);
}

bool layer_locks_position(const Layer& layer) {
  return has_all_flags(layer.lock_flags(), kLayerLockPosition);
}

void set_layer_locks_position(Layer& layer, bool locked) {
  set_layer_lock_flag(layer, kLayerLockPosition, locked);
}

bool layer_locks_all(const Layer& layer) {
  return (layer.lock_flags() & kLayerLockAll) == kLayerLockAll;
}

void set_layer_locks_all(Layer& layer, bool locked) {
  set_layer_lock_flags(layer, locked ? kLayerLockAll : kLayerLockNone);
}

LayerLockFlags layer_lock_flags(const Layer& layer) {
  return layer.lock_flags();
}

void set_layer_lock_flags(Layer& layer, LayerLockFlags flags) {
  layer.set_lock_flags(flags);
}

void set_layer_lock_flag(Layer& layer, LayerLockFlags flag, bool locked) {
  auto flags = layer.lock_flags();
  if (locked) {
    flags |= flag;
  } else {
    flags &= ~flag;
  }
  set_layer_lock_flags(layer, flags);
}

bool layer_is_locked(const Layer& layer) {
  return layer_locks_all(layer);
}

void set_layer_locked(Layer& layer, bool locked) {
  set_layer_locks_all(layer, locked);
}

namespace {

struct LayerLockSearchResult {
  bool found{false};
  LayerLockFlags flags{kLayerLockNone};
  LayerLockFlags ancestor_flags{kLayerLockNone};
};

LayerLockSearchResult find_effective_lock(const std::vector<Layer>& layers, LayerId layer_id,
                                          LayerLockFlags ancestor_flags) {
  for (const auto& layer : layers) {
    const auto effective_flags = ancestor_flags | layer.lock_flags();
    if (layer.id() == layer_id) {
      return LayerLockSearchResult{true, effective_flags, ancestor_flags};
    }
    if (layer.kind() == LayerKind::Group) {
      if (auto found = find_effective_lock(layer.children(), layer_id, effective_flags); found.found) {
        return found;
      }
    }
  }
  return {};
}

}  // namespace

LayerLockFlags layer_effective_lock_flags(const std::vector<Layer>& layers, LayerId layer_id) {
  return find_effective_lock(layers, layer_id, kLayerLockNone).flags & kLayerLockAll;
}

LayerLockFlags layer_ancestor_lock_flags(const std::vector<Layer>& layers, LayerId layer_id) {
  return find_effective_lock(layers, layer_id, kLayerLockNone).ancestor_flags & kLayerLockAll;
}

bool layer_effectively_locks_transparent_pixels(const std::vector<Layer>& layers, LayerId layer_id) {
  return has_any_flags(layer_effective_lock_flags(layers, layer_id), kLayerLockTransparentPixels);
}

bool layer_effectively_locks_image_pixels(const std::vector<Layer>& layers, LayerId layer_id) {
  return has_any_flags(layer_effective_lock_flags(layers, layer_id), kLayerLockImagePixels);
}

bool layer_effectively_locks_position(const std::vector<Layer>& layers, LayerId layer_id) {
  return has_any_flags(layer_effective_lock_flags(layers, layer_id), kLayerLockPosition);
}

bool layer_is_effectively_locked(const std::vector<Layer>& layers, LayerId layer_id) {
  return (layer_effective_lock_flags(layers, layer_id) & kLayerLockAll) == kLayerLockAll;
}

bool layer_has_locked_ancestor(const std::vector<Layer>& layers, LayerId layer_id) {
  return layer_ancestor_lock_flags(layers, layer_id) != kLayerLockNone;
}

bool layer_mask_linked(const Layer& layer) {
  const auto found = layer.metadata().find(kLayerMetadataMaskLinked);
  return found == layer.metadata().end() || found->second != "false";
}

void set_layer_mask_linked(Layer& layer, bool linked) {
  if (linked) {
    layer.metadata().erase(kLayerMetadataMaskLinked);
  } else {
    layer.metadata()[kLayerMetadataMaskLinked] = "false";
  }
}

bool layer_group_expanded(const Layer& layer) {
  const auto found = layer.metadata().find(kLayerMetadataGroupExpanded);
  return found == layer.metadata().end() || found->second != "false";
}

void set_layer_group_expanded(Layer& layer, bool expanded) {
  if (expanded) {
    layer.metadata()[kLayerMetadataGroupExpanded] = "true";
  } else {
    layer.metadata()[kLayerMetadataGroupExpanded] = "false";
  }
}

bool layer_is_text(const Layer& layer) {
  return layer.metadata().contains(kLayerMetadataText);
}

std::optional<LayerAffineTransform> parse_layer_affine_transform(std::string_view text) {
  std::istringstream stream{std::string(text)};
  LayerAffineTransform transform{};
  for (auto& value : transform) {
    if (!(stream >> value) || !std::isfinite(value)) {
      return std::nullopt;
    }
  }
  return transform;
}

std::string serialize_layer_affine_transform(const LayerAffineTransform& transform) {
  std::ostringstream stream;
  stream << std::setprecision(12);
  for (std::size_t i = 0; i < transform.size(); ++i) {
    if (i != 0U) {
      stream << ' ';
    }
    stream << transform[i];
  }
  return stream.str();
}

LayerAffineTransform compose_layer_affine_transform(const LayerAffineTransform& outer,
                                                    const LayerAffineTransform& inner) {
  return LayerAffineTransform{
      outer[0] * inner[0] + outer[2] * inner[1],
      outer[1] * inner[0] + outer[3] * inner[1],
      outer[0] * inner[2] + outer[2] * inner[3],
      outer[1] * inner[2] + outer[3] * inner[3],
      outer[0] * inner[4] + outer[2] * inner[5] + outer[4],
      outer[1] * inner[4] + outer[3] * inner[5] + outer[5],
  };
}

}  // namespace patchy
