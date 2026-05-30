#include "core/layer_metadata.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

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
