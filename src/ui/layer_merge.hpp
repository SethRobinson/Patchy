#pragma once

#include "core/document.hpp"

#include <functional>
#include <optional>
#include <vector>

class QWidget;

namespace patchy::ui {

// Merge Down's appearance-preserving alternative to flattening. See docs/layer-merging.md.
struct LayerMergeOptions {
  bool keep_vectors{true};
  bool within_groups{false};
  bool separate_vector_types{true};
};

struct LayerMergeNode {
  std::vector<LayerId> sources;
  std::vector<LayerMergeNode> children;
  bool rebuild_group{false};
  bool selected{false};
  bool mergeable{false};
  bool vector{false};
  bool rasterize{false};
  bool changed{false};
};

struct LayerMergePlan {
  std::vector<LayerMergeNode> roots;
  std::vector<LayerId> result_ids;
  std::size_t removed_layers{0};
  std::size_t vector_layers{0};
  std::size_t bitmap_layers{0};
  std::size_t kept_layers{0};
  bool changed{false};
};

[[nodiscard]] bool merge_selection_contains_vectors(const Document& document, const std::vector<LayerId>& ids);
// Read-only: never bakes pixels or changes document revisions. Unselected layers,
// clipping chains, locks, masks and backdrop-dependent appearances are barriers.
[[nodiscard]] LayerMergePlan plan_layer_merge(const Document& document, const std::vector<LayerId>& ids,
                                             LayerMergeOptions options = {});
// Prepare before arming undo. A failed bake leaves the source and history intact.
[[nodiscard]] Document render_layer_merge(
    const Document& document, const LayerMergePlan& plan,
    const std::function<std::optional<Layer>(const Layer&)>& raster_source = {});
[[nodiscard]] std::optional<LayerMergeOptions> show_layer_merge_dialog(
    QWidget* parent, const Document& document, const std::vector<LayerId>& ids);

}  // namespace patchy::ui
