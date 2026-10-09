#pragma once

#include "core/layer_metadata.hpp"
#include "core/vector_shape.hpp"

#include <cmath>

namespace patchy {

// A text-owned, closed contour in text-local coordinates. The text transform maps
// it to document space; editing the original shape cannot change this copy.
// Photoshop's area frame contains one cubic contour (docs/area-text.md).
inline bool valid_text_area(const VectorPath& path) {
  if (path.subpaths.size() != 1 || !path.subpaths.front().closed ||
      path.subpaths.front().anchors.size() < 2 || path.subpaths.front().anchors.size() > 100000) {
    return false;
  }
  for (const auto& p : path.subpaths.front().anchors) {
    for (const double v : {p.anchor_x, p.anchor_y, p.in_x, p.in_y, p.out_x, p.out_y}) {
      if (!std::isfinite(v) || std::abs(v) > 1000000.0) return false;
    }
  }
  return true;
}

inline std::optional<VectorPath> text_area_for_layer(const Layer& layer) {
  const auto it = layer.metadata().find(kLayerMetadataTextArea);
  if (it == layer.metadata().end()) return std::nullopt;
  auto path = parse_vector_path(it->second);
  if (path && valid_text_area(*path)) {
    path->fill_rule_value = 0;
    path->initial_fill_value = 0;
    path->subpaths.front().op = PathCombineOp::Xor;
    path->subpaths.front().shape_group = 0;
  }
  return path && valid_text_area(*path) ? std::move(path) : std::nullopt;
}

inline bool text_geometry_is_protected(const Layer& layer) {
  return layer.metadata().contains(kLayerMetadataTextGeometryProtected);
}

inline LayerAffineTransform text_area_transform(const Layer& layer) {
  for (const auto* key : {kLayerMetadataTextTransform, kLayerMetadataPsdTextTransform}) {
    const auto it = layer.metadata().find(key);
    if (it != layer.metadata().end()) {
      if (const auto transform = parse_layer_affine_transform(it->second)) return *transform;
    }
  }
  return {1.0, 0.0, 0.0, 1.0, static_cast<double>(layer.bounds().x), static_cast<double>(layer.bounds().y)};
}

inline std::optional<VectorPath> text_area_in_document(const Layer& layer) {
  auto path = text_area_for_layer(layer);
  if (path) transform_vector_path(*path, text_area_transform(layer));
  return path;
}

}  // namespace patchy
