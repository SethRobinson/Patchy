#pragma once

#include "core/document.hpp"

#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

class QWidget;

namespace patchy::ui {
class CanvasWidget;

// Merge Down's appearance-preserving alternative to flattening. See docs/layer-merging.md.
struct LayerMergeOptions {
  bool keep_vectors{true};
  bool within_groups{false};
  bool separate_vector_types{true};
  bool hide_originals{true}; // Copy dialog only; Merge Down never changes this.
  bool single_vector{false}; // Explicitly permits effect removal and changes to stacking order.
  std::optional<LayerId> effects_source{}; // Single-vector mode: absent removes effects.
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
  bool replace_effects{false};
  std::optional<LayerId> effects_source{};
};

enum class LayerMergeBlocker {
  TooFewVectors, NotVector, Locked, Hidden, Clipping, Mask, Filters, Blending,
  UnsupportedVector, OpacityBoundary, GroupBoundary, InvalidEffectsSource, VectorEdges
};

struct LayerMergeIssue {
  LayerId layer_id{0};
  LayerMergeBlocker reason{LayerMergeBlocker::NotVector};
};

struct LayerMergePlan {
  std::vector<LayerMergeNode> roots;
  std::vector<LayerId> result_ids;
  std::size_t removed_layers{0};
  std::size_t vector_layers{0};
  std::size_t bitmap_layers{0};
  std::size_t kept_layers{0};
  bool changed{false};
  bool changes_stacking_order{false};
  std::vector<LayerMergeIssue> blockers;
};

[[nodiscard]] QStringList layer_merge_blocker_messages(const Document& document, const LayerMergePlan& plan);

[[nodiscard]] bool merge_selection_contains_vectors(const Document& document, const std::vector<LayerId>& ids);
// Read-only: never bakes pixels or changes document revisions. Unselected layers,
// clipping chains, locks, masks and backdrop-dependent appearances are barriers
// unless an explicit single-vector policy permits changing effects/stacking.
[[nodiscard]] LayerMergePlan plan_layer_merge(const Document& document, const std::vector<LayerId>& ids,
                                             LayerMergeOptions options = {}, bool copy = false);
[[nodiscard]] Document visible_document_for_merge_copy(const Document& document);
// Merge Selected to New Layer (GitHub issue 475; docs/layer-merging.md): the document cut down
// to what the selected layers draw. A selected group keeps its visible subtree; an unselected
// group holding selected layers dissolves into its parent (the copy lands inside it, so its
// opacity, mask and blending still apply once). Hidden layers and layers clipped to a hidden
// base drop, as in visible_document_for_merge_copy; a clipped layer whose base is not part of
// the merge is released. Locks are cleared on the copies. No layers when nothing selected draws.
[[nodiscard]] Document selected_document_for_merge_copy(const Document& document, const std::vector<LayerId>& ids);
// Merge Visible in place (docs/layer-merging.md, "Merge Visible"). A layer draws when it and
// every ancestor are visible and it is not clipped to a hidden base.
// Layers other than groups that draw; Merge Visible needs two, like Photoshop.
[[nodiscard]] std::size_t merge_visible_layer_count(const Document& document);
// The layer whose id, name and stack slot the merged result takes: a visible Background,
// else the active layer when it draws, else the topmost layer that draws (0 when none does).
[[nodiscard]] LayerId merge_visible_target(const Document& document);
// The layer stack after Merge Visible: every layer that draws is gone and `merged` sits in
// `target`'s slot. Visible groups dissolve, so the layers that stay (hidden ones with their
// subtrees, clipped layers over a hidden base) keep their order at the top level, as in
// Photoshop; a kept clipped layer left with nothing below it is released.
[[nodiscard]] std::vector<Layer> merge_visible_layer_stack(const Document& document, LayerId target, Layer merged);
// Prepare before arming undo. A failed bake leaves the source and history intact.
[[nodiscard]] Document render_layer_merge(
    const Document& document, const LayerMergePlan& plan,
    const std::function<std::optional<Layer>(const Layer&)>& raster_source = {});
[[nodiscard]] Document render_layer_merge_with_processing(
    CanvasWidget* canvas, const Document& document, const LayerMergePlan& plan,
    const std::function<std::optional<Layer>(const Layer&)>& raster_source = {});
[[nodiscard]] std::optional<LayerMergeOptions> show_layer_merge_dialog(
    QWidget* parent, const Document& document, const std::vector<LayerId>& ids, bool copy = false,
    std::optional<Document>* prepared_result = nullptr);

}  // namespace patchy::ui
