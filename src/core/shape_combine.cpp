#include "core/shape_combine.hpp"

#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "core/vector_raster.hpp"

#include <algorithm>
#include <utility>

namespace patchy {

ShapeCombineCandidates combine_shape_candidates(const std::vector<Layer>& layers, const std::vector<LayerId>& ids) {
  ShapeCombineCandidates result;
  const auto roots = root_drop_layer_ids(layers, ids);
  if (roots.size() < 2) {
    result.refusal = ShapeCombineRefusal::NeedTwoLayers;
    return result;
  }
  const std::vector<Layer>* siblings = nullptr;
  std::vector<std::pair<std::size_t, LayerId>> ordered;
  for (const auto id : roots) {
    const auto location = find_layer_location(layers, id);
    if (!location.has_value() || location->siblings == nullptr) {
      result.refusal = ShapeCombineRefusal::NotShapeLayer;
      return result;
    }
    const auto& layer = (*location->siblings)[location->index];
    if (!layer_is_vector_shape(layer) || layer.vector_shape() == nullptr) {
      result.refusal = ShapeCombineRefusal::NotShapeLayer;
      return result;
    }
    if (!vector_lock_reason(layer).empty() || layer_effectively_locks_image_pixels(layers, id) ||
        layer_is_effectively_locked(layers, id)) {
      result.refusal = ShapeCombineRefusal::Locked;
      return result;
    }
    if (layer.vector_shape()->path.empty()) {
      result.refusal = ShapeCombineRefusal::EmptyPath;  // a whole-canvas fill layer
      return result;
    }
    if (siblings == nullptr) {
      siblings = location->siblings;
    } else if (siblings != location->siblings) {
      result.refusal = ShapeCombineRefusal::DifferentParents;
      return result;
    }
    ordered.emplace_back(location->index, id);
  }
  // layers()[0] composites first, so the lowest sibling index is the bottom.
  std::sort(ordered.begin(), ordered.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  for (const auto& [index, id] : ordered) {
    result.bottom_to_top.push_back(id);
  }
  result.refusal = ShapeCombineRefusal::None;
  return result;
}

void append_shape_groups(VectorShapeContent& base, const VectorPath& front, PathCombineOp op) {
  auto next_group = base.path.next_shape_group();
  std::vector<std::pair<std::int32_t, std::int32_t>> remap;  // front group -> base group
  for (const auto& subpath : front.subpaths) {
    auto copy = subpath;
    const auto mapped = std::find_if(remap.begin(), remap.end(), [&subpath](const auto& entry) {
      return entry.first == subpath.shape_group;
    });
    if (mapped == remap.end()) {
      remap.emplace_back(subpath.shape_group, next_group);
      copy.shape_group = next_group++;
    } else {
      copy.shape_group = mapped->second;
    }
    copy.op = op;
    base.path.subpaths.push_back(std::move(copy));
  }
}

std::optional<ShapeCombineResult> combine_shape_layers(Document& document,
                                                       const std::vector<LayerId>& bottom_to_top,
                                                       PathCombineOp op) {
  if (bottom_to_top.size() < 2) {
    return std::nullopt;
  }
  const auto base_id = bottom_to_top.front();
  const auto* base_view = std::as_const(document).find_layer(base_id);
  if (base_view == nullptr || base_view->vector_shape() == nullptr) {
    return std::nullopt;
  }
  auto content = *base_view->vector_shape();
  for (std::size_t i = 1; i < bottom_to_top.size(); ++i) {
    const auto* front = std::as_const(document).find_layer(bottom_to_top[i]);
    if (front == nullptr || front->vector_shape() == nullptr) {
      return std::nullopt;
    }
    append_shape_groups(content, front->vector_shape()->path, op);
  }
  auto* base = document.find_layer(base_id);
  if (base == nullptr) {
    return std::nullopt;
  }
  base->set_vector_shape(std::move(content));
  base->metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
  mark_layer_vector_block_dirty(*base);
  update_vector_shape_raster(*base, Rect::from_size(document.width(), document.height()),
                             &document.metadata().patterns);
  ShapeCombineResult result;
  result.layer_id = base_id;
  for (std::size_t i = 1; i < bottom_to_top.size(); ++i) {
    if (document.remove_layer(bottom_to_top[i])) {
      ++result.removed_layers;
    }
  }
  return result;
}

}  // namespace patchy
