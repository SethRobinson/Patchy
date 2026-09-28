#pragma once

// Align and Distribute geometry for the Layer > Arrange commands and the Move
// tool's options-bar buttons (docs/alignment.md). Pure integer/double math over
// document-space rects, Qt-free and deterministic: the UI, the scripting API,
// and the core tests all read the same deltas from here.
//
// Legal boundary (docs/legal-constraints.md, "Alignment guides and
// Align/Distribute"): these are user-invoked, one-shot layout commands with no
// reposition input, which is what keeps static Distribute outside Adobe
// US 10782861 (equidistant placement DURING a drag). Never wire these spacing
// computations into the Move drag's snapping.

#include "core/layer.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace patchy {

enum class AlignEdge {
  Left,
  HorizontalCenter,
  Right,
  Top,
  VerticalCenter,
  Bottom,
};

enum class DistributeMode {
  Left,
  HorizontalCenter,
  Right,
  Top,
  VerticalCenter,
  Bottom,
  HorizontalSpacing,
  VerticalSpacing,
};

struct AlignmentOffset {
  std::int32_t dx{0};
  std::int32_t dy{0};

  [[nodiscard]] bool is_zero() const noexcept { return dx == 0 && dy == 0; }
};

// True for the edges that move units along X (Left, HorizontalCenter, Right).
[[nodiscard]] bool align_edge_is_horizontal(AlignEdge edge) noexcept;
// True for the modes that move units along X.
[[nodiscard]] bool distribute_mode_is_horizontal(DistributeMode mode) noexcept;

// Permanent script identifiers ("left", "hcenter", "right", "top", "vcenter",
// "bottom", plus "hspacing" / "vspacing" for Distribute). Never rename.
[[nodiscard]] std::string_view align_edge_id(AlignEdge edge) noexcept;
[[nodiscard]] std::string_view distribute_mode_id(DistributeMode mode) noexcept;
[[nodiscard]] std::optional<AlignEdge> align_edge_from_id(std::string_view id) noexcept;
[[nodiscard]] std::optional<DistributeMode> distribute_mode_from_id(std::string_view id) noexcept;

// One offset per unit rect, in input order, that lines the unit's `edge`
// feature up with the same feature of `reference`. Centers are x + width / 2.0
// and every result rounds with std::lround. Empty unit rects get a zero offset.
[[nodiscard]] std::vector<AlignmentOffset> compute_align_deltas(const std::vector<Rect>& units, Rect reference,
                                                                AlignEdge edge);

// One offset per unit rect, in input order. Feature modes sort the units by
// the feature, keep the first and last where they are, and place the
// intermediate features evenly between them. Spacing modes sort by the leading
// edge, keep the outermost units, and lay the rest out with one equal gap; when
// the units are wider than the span the gap goes negative and they overlap
// evenly (Photoshop does the same). Fewer than three units return zero offsets.
[[nodiscard]] std::vector<AlignmentOffset> compute_distribute_deltas(const std::vector<Rect>& units,
                                                                     DistributeMode mode);

}  // namespace patchy
