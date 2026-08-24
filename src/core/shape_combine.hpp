#pragma once

#include "core/document.hpp"
#include "core/vector_shape.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Combine Shapes: merges several shape layers into one the way the
// sequential combine renderer already composes shape groups. The BOTTOM-most
// selected layer is the base and keeps its id, name, appearance, styles,
// masks, and its groups' own ops; every group of each front layer is
// appended in stacking order with the chosen op (Unite = Add, Subtract Front
// Shape = Subtract, Intersect = Intersect, Exclude Overlapping = Xor). Front
// origination is not carried over (a geometry edit); front layers' own
// styles and masks vanish with the layers. Semantics: docs/vector-commands.md.
namespace patchy {

enum class ShapeCombineRefusal : std::uint8_t {
  None,
  NeedTwoLayers,
  NotShapeLayer,
  Locked,
  EmptyPath,
  DifferentParents
};

struct ShapeCombineCandidates {
  std::vector<LayerId> bottom_to_top;  // sibling order; the base comes first
  ShapeCombineRefusal refusal{ShapeCombineRefusal::NeedTwoLayers};
};

// Validates a Layers-panel selection: two or more editable shape layers with
// paths, all siblings of one parent (descendants of a selected folder are
// ignored, like every multi-layer command).
[[nodiscard]] ShapeCombineCandidates combine_shape_candidates(const std::vector<Layer>& layers,
                                                              const std::vector<LayerId>& ids);

// Appends every subpath of `front` to `base` under `op`; the front's shape
// groups are renumbered from base.path.next_shape_group() in first-appearance
// order so they stay distinct units.
void append_shape_groups(VectorShapeContent& base, const VectorPath& front, PathCombineOp op);

struct ShapeCombineResult {
  LayerId layer_id{0};
  std::size_t removed_layers{0};
};

// Performs the merge on the document (base first in `bottom_to_top`), re-bakes
// the base, and removes the fronts. nullopt when a layer is missing or not a
// shape layer; validate with combine_shape_candidates first.
[[nodiscard]] std::optional<ShapeCombineResult> combine_shape_layers(Document& document,
                                                                     const std::vector<LayerId>& bottom_to_top,
                                                                     PathCombineOp op);

}  // namespace patchy
