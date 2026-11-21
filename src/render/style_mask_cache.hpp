#pragma once

#include "render/layer_render_types.hpp"

#include <utility>

namespace patchy::render_detail {

// Shared miss/hit flow: returns the mask (cached or computed) plus the domain
// it covers. compute(domain) must return the prepared primary/secondary masks
// for exactly that domain.
template <typename ComputeFn>
std::pair<std::shared_ptr<const StyleMaskEntry>, Rect> style_mask_for_render(
    StyleMaskProvider* provider, const Layer& layer, StyleMaskKind kind, std::uint32_t effect_index,
    Rect full_domain, Rect gate_rect, Rect legacy_domain, Rect bounds, std::optional<Rect> mask_bounds,
    ComputeFn&& compute) {
  const bool group = layer.kind() == LayerKind::Group;
  // A stroke's patch windows the effect masks of the groups it paints inside, as it does
  // the painted layer's. The silhouette itself always covers the whole group.
  const bool windowed_group =
      group && kind != StyleMaskKind::GroupSilhouette && provider != nullptr && provider->group_masks_windowed(layer);
  if (windowed_group) {
    return {std::make_shared<StyleMaskEntry>(compute(legacy_domain)), legacy_domain};
  }
  if (provider != nullptr && (group || provider->full_domain_allowed(layer, gate_rect))) {
    if (auto cached = provider->fetch(layer, kind, effect_index, full_domain, bounds, mask_bounds);
        cached != nullptr) {
      return {std::move(cached), full_domain};
    }
    // A null fetch may have latched the key as in-flight; store() (entry or
    // null) MUST follow or concurrent renders of this effect block forever.
    std::shared_ptr<StyleMaskEntry> computed;
    try {
      computed = std::make_shared<StyleMaskEntry>(compute(full_domain));
    } catch (...) {
      provider->store(layer, kind, effect_index, full_domain, bounds, mask_bounds, nullptr);
      throw;
    }
    provider->store(layer, kind, effect_index, full_domain, bounds, mask_bounds, computed);
    return {std::move(computed), full_domain};
  }
  const auto domain = layer.kind() == LayerKind::Group ? full_domain : legacy_domain;
  return {std::make_shared<StyleMaskEntry>(compute(domain)), domain};
}

}  // namespace patchy::render_detail
