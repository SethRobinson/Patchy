#pragma once

#include "core/layer.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace patchy {

// Quick Select: brush-seeded foreground extraction (seeded min-cut segmentation with
// foreground/background color histograms, in the lineage of Boykov-Jolly 2001 / GrabCut 2004).
// The caller accumulates a brush-footprint seed mask while the user drags and calls
// quick_select_segment() once per stroke, after the input gesture completes. Nothing here may
// run per-mouse-move classification while a stroke is still being drawn: live classify-and
// -display during brush input is claimed by Adobe's US 8050498 until Nov 3, 2029.
struct QuickSelectParams {
  int brush_radius{16};
  bool subtract{false};      // stroke removes from the base selection instead of adding
  bool enhance_edge{false};  // geometric boundary smoothing of the result (no color models)
  // Smoothness weight against the [0, 10] posterior data costs. Deliberately
  // boundary-dominant (calibrated against Photoshop 2026): the cut snaps to the strongest
  // enclosing contour within the spread budget — an eye click takes the whole eye opening
  // including the catchlights — while in featureless areas the minimum contour is the brush
  // rim itself, so a flat-area click stays disc-sized.
  double lambda{8.0};
  // How far the selection may grow beyond the brush footprint, as with Photoshop's hidden
  // quickSelectSpread option (0-100, 50 = PS default). The growth budget is proportional to
  // the brush radius; edges snap freely inside it and a steep cost wall stops growth past it.
  // This is what makes a tap behave like a tap instead of a magic-wand flood.
  int spread{50};
  int background_samples{1200};
  int min_window_padding{32};  // solve window = seed bbox dilated to cover the spread budget
  std::uint32_t rng_seed{0x9E3779B9u};  // background sampling is deterministic for tests
};

// One horizontal span of changed pixels in document space; x0/x1 are inclusive.
struct QuickSelectRun {
  std::int32_t y{0};
  std::int32_t x0{0};
  std::int32_t x1{0};
};

// Pixels the stroke selects (add) or deselects (subtract). delta_mask is 0/255, row-major,
// delta_bounds.width * delta_bounds.height bytes; empty when the stroke changed nothing.
struct QuickSelectResult {
  std::vector<std::uint8_t> delta_mask;
  Rect delta_bounds{};
  std::vector<QuickSelectRun> delta_runs;

  [[nodiscard]] bool empty() const noexcept { return delta_runs.empty(); }
};

// Segments one completed stroke. `rgba` is RGBA8888 rows (`stride_bytes` apart) of the image
// being sampled. `base_selection` is the doc-sized 0/255 mask of the selection before the
// stroke (null = empty selection). `seed_mask` is the doc-sized 0/255 brush footprint with
// `seed_bounds` its bounding rect. Seeds already inside the base selection are ignored when
// adding (and seeds outside it when subtracting), so dragging over already-selected area is a
// cheap no-op.
[[nodiscard]] QuickSelectResult quick_select_segment(const std::uint8_t* rgba, std::int32_t width,
                                                     std::int32_t height, std::ptrdiff_t stride_bytes,
                                                     const std::uint8_t* base_selection,
                                                     const std::uint8_t* seed_mask, Rect seed_bounds,
                                                     const QuickSelectParams& params);

// "Enhance Edge": a purely geometric 3x3 majority smoothing of a 0/255 mask,
// restricted to `band` (intersected with the mask rect). Two passes; deliberately involves no
// color models (local color-model edge refinement is claimed by Adobe's US 8013870 until 2028).
void enhance_selection_edge(std::vector<std::uint8_t>& mask, std::int32_t width, std::int32_t height,
                            Rect band);

namespace detail {

// Max-flow/min-cut on an 8-connected 2D grid, implemented from the Boykov-Kolmogorov
// algorithm description (PAMI 2004): dual search trees with active/orphan queues and the
// timestamp/distance adoption heuristics. Arcs are the fixed 8 grid directions per node, so
// no adjacency lists are needed. Exposed for unit tests.
class GridMaxflow {
 public:
  GridMaxflow(std::int32_t width, std::int32_t height);

  // Net terminal capacities for one node; call at most once per node. Capacities must be
  // non-negative. min(source_cap, sink_cap) is accounted into the flow immediately and only
  // the difference is stored.
  void set_terminal_caps(std::int32_t index, float source_cap, float sink_cap);
  // Symmetric n-link between two grid-adjacent nodes (both directions get `cap`).
  void set_neighbor_cap(std::int32_t index_a, std::int32_t index_b, float cap);

  double solve();
  // Valid after solve(): true when the node is on the source side of the min cut (free nodes
  // report sink side).
  [[nodiscard]] bool is_source_side(std::int32_t index) const;

 private:
  static constexpr std::int32_t kDirections = 8;

  [[nodiscard]] std::int32_t neighbor(std::int32_t index, std::int32_t direction) const noexcept;
  void grow(std::int32_t& contact_node, std::int32_t& contact_direction);
  void augment(std::int32_t contact_node, std::int32_t contact_direction);
  void adopt();
  void process_orphan(std::int32_t node);
  [[nodiscard]] bool has_root_path(std::int32_t node);

  std::int32_t width_{0};
  std::int32_t height_{0};
  std::int32_t node_count_{0};
  std::vector<float> arc_cap_;      // node * 8 + direction
  std::vector<float> terminal_cap_; // > 0: residual from source, < 0: residual to sink
  std::vector<std::uint8_t> tree_;  // 0 free, 1 source, 2 sink
  std::vector<std::int8_t> parent_; // direction to parent, or kParentTerminal / kParentNone
  std::vector<std::uint32_t> timestamp_;
  std::vector<std::int32_t> distance_;
  std::vector<std::int32_t> active_;
  std::size_t active_head_{0};
  std::vector<std::int32_t> orphans_;
  std::uint32_t time_{0};
  double flow_{0.0};
};

}  // namespace detail
}  // namespace patchy
