#include "core/layer_metadata.hpp"

namespace patchy {

bool layer_locks_transparent_pixels(const Layer& layer) {
  const auto found = layer.metadata().find(kLayerMetadataLockTransparentPixels);
  return found != layer.metadata().end() && found->second == "true";
}

void set_layer_locks_transparent_pixels(Layer& layer, bool locked) {
  if (locked) {
    layer.metadata()[kLayerMetadataLockTransparentPixels] = "true";
  } else {
    layer.metadata().erase(kLayerMetadataLockTransparentPixels);
  }
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

}  // namespace patchy
