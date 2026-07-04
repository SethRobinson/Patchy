#include "core/quick_select.hpp"

#include "core/rect_utils.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>

namespace patchy {

namespace {

constexpr float kHardCap = 1.0e6f;
constexpr double kDataCostClamp = 10.0;
// Class prior against selecting (see the data-term comment at the t-link loop). Calibrated
// against Photoshop 2026: a click on a featureless area must return roughly the brush disc
// (expansion requires evidence; the thresholded edge model already prevents weak-contour
// creep, so this only needs to damp free-floating neutral pixels).
constexpr double kBackgroundPrior = 0.35;
// Cap on solve-grid nodes; larger windows are solved on a power-of-two downsample and the
// labels are re-expanded during finalization. Keeps a worst-case stroke solve in the tens of
// milliseconds instead of seconds.
constexpr std::int32_t kMaxSolveNodes = 600'000;
constexpr int kHistogramCells = 16 * 16 * 16;

struct RgbaSample {
  std::uint8_t r{0};
  std::uint8_t g{0};
  std::uint8_t b{0};
  std::uint8_t a{0};
};

// Colors are compared premultiplied so fully transparent pixels look identical regardless of
// their hidden RGB, and semi-transparent edges grade smoothly toward the backdrop-free color.
RgbaSample premultiplied_sample(const std::uint8_t* rgba, std::ptrdiff_t stride_bytes, std::int32_t x,
                                std::int32_t y) {
  const auto* px = rgba + static_cast<std::ptrdiff_t>(y) * stride_bytes + static_cast<std::ptrdiff_t>(x) * 4;
  const int alpha = px[3];
  RgbaSample sample;
  sample.r = static_cast<std::uint8_t>((px[0] * alpha + 127) / 255);
  sample.g = static_cast<std::uint8_t>((px[1] * alpha + 127) / 255);
  sample.b = static_cast<std::uint8_t>((px[2] * alpha + 127) / 255);
  sample.a = static_cast<std::uint8_t>(alpha);
  return sample;
}

int histogram_cell(RgbaSample color) noexcept {
  return ((color.r >> 4) << 8) | ((color.g >> 4) << 4) | (color.b >> 4);
}

int color_distance_squared(RgbaSample lhs, RgbaSample rhs) noexcept {
  const int dr = static_cast<int>(lhs.r) - rhs.r;
  const int dg = static_cast<int>(lhs.g) - rhs.g;
  const int db = static_cast<int>(lhs.b) - rhs.b;
  const int da = static_cast<int>(lhs.a) - rhs.a;
  return dr * dr + dg * dg + db * db + da * da;
}

// Lightly smoothed RGB frequency histogram (4-bit per channel). Deliberately a full
// distribution: the data term below compares likelihoods, never "average model colors".
struct ColorHistogram {
  std::array<float, kHistogramCells> cells{};
  double total{0.0};

  void add(RgbaSample color) {
    cells[static_cast<std::size_t>(histogram_cell(color))] += 1.0f;
    total += 1.0;
  }

  // Raw relative frequency, no smoothing. The posterior below adds a SYMMETRIC epsilon to
  // both models: per-model Laplace floors scale with 1/total, so the smaller model (the
  // stroke) gets the bigger floor and every color absent from BOTH models silently leans
  // foreground — on photos that flooded taps to the window border (July 2026).
  [[nodiscard]] double frequency(RgbaSample color) const {
    return total > 0.0 ? cells[static_cast<std::size_t>(histogram_cell(color))] / total : 0.0;
  }
};

struct XorShift32 {
  std::uint32_t state;

  explicit XorShift32(std::uint32_t seed) : state(seed != 0 ? seed : 0x9E3779B9u) {}

  std::uint32_t next() noexcept {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
};

Rect dilated_rect(Rect rect, std::int32_t amount) noexcept {
  return Rect{rect.x - amount, rect.y - amount, rect.width + amount * 2, rect.height + amount * 2};
}

// 3x3 center-weighted majority vote over 0/255 masks; `protect` (optional, same layout) pins
// pixels that must not flip (seeds, pre-existing selection, hard window borders). The center
// counts double so ties keep the current value: a plain majority oscillates on 1px comb
// patterns instead of smoothing them.
void majority_smooth(std::uint8_t* mask, std::int32_t width, std::int32_t height, Rect band,
                     const std::uint8_t* protect, int iterations) {
  band = intersect_rect(band, Rect::from_size(width, height));
  if (band.empty()) {
    return;
  }
  std::vector<std::uint8_t> scratch(static_cast<std::size_t>(band.width) * band.height);
  for (int iteration = 0; iteration < iterations; ++iteration) {
    for (std::int32_t y = band.y; y < band.y + band.height; ++y) {
      for (std::int32_t x = band.x; x < band.x + band.width; ++x) {
        const std::size_t index = static_cast<std::size_t>(y) * width + x;
        const std::size_t out = static_cast<std::size_t>(y - band.y) * band.width + (x - band.x);
        if (protect != nullptr && protect[index] != 0) {
          scratch[out] = mask[index];
          continue;
        }
        int votes = mask[index] != 0 ? 1 : 0;  // center counted twice in total
        for (int dy = -1; dy <= 1; ++dy) {
          const std::int32_t sy = std::clamp<std::int32_t>(y + dy, 0, height - 1);
          for (int dx = -1; dx <= 1; ++dx) {
            const std::int32_t sx = std::clamp<std::int32_t>(x + dx, 0, width - 1);
            votes += mask[static_cast<std::size_t>(sy) * width + sx] != 0 ? 1 : 0;
          }
        }
        scratch[out] = votes >= 6 ? 255 : 0;  // of 10 weighted votes
      }
    }
    for (std::int32_t y = band.y; y < band.y + band.height; ++y) {
      std::memcpy(mask + static_cast<std::size_t>(y) * width + band.x,
                  scratch.data() + static_cast<std::size_t>(y - band.y) * band.width,
                  static_cast<std::size_t>(band.width));
    }
  }
}

}  // namespace

void enhance_selection_edge(std::vector<std::uint8_t>& mask, std::int32_t width, std::int32_t height,
                            Rect band) {
  if (width <= 0 || height <= 0 || mask.size() < static_cast<std::size_t>(width) * height) {
    return;
  }
  majority_smooth(mask.data(), width, height, band, nullptr, 2);
}

namespace detail {

namespace {

constexpr std::int8_t kParentNone = -1;
constexpr std::int8_t kParentTerminal = -2;
constexpr std::uint8_t kTreeFree = 0;
constexpr std::uint8_t kTreeSource = 1;
constexpr std::uint8_t kTreeSink = 2;

constexpr std::array<std::int32_t, 8> kDirDx{1, -1, 0, 0, 1, -1, 1, -1};
constexpr std::array<std::int32_t, 8> kDirDy{0, 0, -1, 1, -1, -1, 1, 1};
// Direction of the reverse arc: E<->W, N<->S, NE<->SW, NW<->SE.
constexpr std::array<std::int8_t, 8> kOpposite{1, 0, 3, 2, 7, 6, 5, 4};

}  // namespace

GridMaxflow::GridMaxflow(std::int32_t width, std::int32_t height)
    : width_(width),
      height_(height),
      node_count_(width * height),
      arc_cap_(static_cast<std::size_t>(node_count_) * kDirections, 0.0f),
      terminal_cap_(static_cast<std::size_t>(node_count_), 0.0f),
      tree_(static_cast<std::size_t>(node_count_), kTreeFree),
      parent_(static_cast<std::size_t>(node_count_), kParentNone),
      timestamp_(static_cast<std::size_t>(node_count_), 0),
      distance_(static_cast<std::size_t>(node_count_), 0) {
  assert(width_ > 0 && height_ > 0);
}

std::int32_t GridMaxflow::neighbor(std::int32_t index, std::int32_t direction) const noexcept {
  const std::int32_t x = index % width_ + kDirDx[static_cast<std::size_t>(direction)];
  const std::int32_t y = index / width_ + kDirDy[static_cast<std::size_t>(direction)];
  if (x < 0 || x >= width_ || y < 0 || y >= height_) {
    return -1;
  }
  return y * width_ + x;
}

void GridMaxflow::set_terminal_caps(std::int32_t index, float source_cap, float sink_cap) {
  assert(index >= 0 && index < node_count_);
  assert(source_cap >= 0.0f && sink_cap >= 0.0f);
  flow_ += std::min(source_cap, sink_cap);
  terminal_cap_[static_cast<std::size_t>(index)] = source_cap - sink_cap;
}

void GridMaxflow::set_neighbor_cap(std::int32_t index_a, std::int32_t index_b, float cap) {
  assert(cap >= 0.0f);
  for (std::int32_t direction = 0; direction < kDirections; ++direction) {
    if (neighbor(index_a, direction) == index_b) {
      arc_cap_[static_cast<std::size_t>(index_a) * kDirections + static_cast<std::size_t>(direction)] = cap;
      arc_cap_[static_cast<std::size_t>(index_b) * kDirections +
               static_cast<std::size_t>(kOpposite[static_cast<std::size_t>(direction)])] = cap;
      return;
    }
  }
  assert(false && "set_neighbor_cap called for non-adjacent nodes");
}

bool GridMaxflow::is_source_side(std::int32_t index) const {
  return tree_[static_cast<std::size_t>(index)] == kTreeSource;
}

// Walks a node's parent chain; returns the node's tree distance when the chain reaches a
// terminal (marking the walked prefix with the current timestamp so repeat checks shortcut),
// or -1 when the chain dead-ends in an orphan.
bool GridMaxflow::has_root_path(std::int32_t node) {
  std::int32_t total = 0;
  std::int32_t walk = node;
  while (true) {
    if (timestamp_[static_cast<std::size_t>(walk)] == time_) {
      total += distance_[static_cast<std::size_t>(walk)];
      break;
    }
    const std::int8_t parent_direction = parent_[static_cast<std::size_t>(walk)];
    if (parent_direction == kParentTerminal) {
      total += 1;
      break;
    }
    if (parent_direction == kParentNone) {
      return false;
    }
    walk = neighbor(walk, parent_direction);
    ++total;
  }
  std::int32_t remaining = total;
  walk = node;
  while (timestamp_[static_cast<std::size_t>(walk)] != time_) {
    timestamp_[static_cast<std::size_t>(walk)] = time_;
    distance_[static_cast<std::size_t>(walk)] = remaining;
    const std::int8_t parent_direction = parent_[static_cast<std::size_t>(walk)];
    if (parent_direction == kParentTerminal) {
      break;
    }
    walk = neighbor(walk, parent_direction);
    --remaining;
  }
  return true;
}

void GridMaxflow::grow(std::int32_t& contact_node, std::int32_t& contact_direction) {
  contact_node = -1;
  contact_direction = -1;
  while (active_head_ < active_.size()) {
    const std::int32_t node = active_[active_head_];
    const std::uint8_t tree = tree_[static_cast<std::size_t>(node)];
    if (tree == kTreeFree) {
      ++active_head_;
      continue;
    }
    for (std::int32_t direction = 0; direction < kDirections; ++direction) {
      const std::int32_t other = neighbor(node, direction);
      if (other < 0) {
        continue;
      }
      // Residual in the tree's growth direction: away from the source for the S tree,
      // toward the sink for the T tree.
      const float residual =
          tree == kTreeSource
              ? arc_cap_[static_cast<std::size_t>(node) * kDirections + static_cast<std::size_t>(direction)]
              : arc_cap_[static_cast<std::size_t>(other) * kDirections +
                         static_cast<std::size_t>(kOpposite[static_cast<std::size_t>(direction)])];
      if (residual <= 0.0f) {
        continue;
      }
      const std::uint8_t other_tree = tree_[static_cast<std::size_t>(other)];
      if (other_tree == kTreeFree) {
        tree_[static_cast<std::size_t>(other)] = tree;
        parent_[static_cast<std::size_t>(other)] = kOpposite[static_cast<std::size_t>(direction)];
        timestamp_[static_cast<std::size_t>(other)] = timestamp_[static_cast<std::size_t>(node)];
        distance_[static_cast<std::size_t>(other)] = distance_[static_cast<std::size_t>(node)] + 1;
        active_.push_back(other);
      } else if (other_tree != tree) {
        // Trees met: report the contact arc oriented source-side -> sink-side. The current
        // node stays active; it may still have unexplored arcs.
        if (tree == kTreeSource) {
          contact_node = node;
          contact_direction = direction;
        } else {
          contact_node = other;
          contact_direction = kOpposite[static_cast<std::size_t>(direction)];
        }
        return;
      } else if (timestamp_[static_cast<std::size_t>(other)] <= timestamp_[static_cast<std::size_t>(node)] &&
                 distance_[static_cast<std::size_t>(other)] > distance_[static_cast<std::size_t>(node)] + 1) {
        // Same tree, but this arc gives a shorter path to the root (BK heuristic).
        parent_[static_cast<std::size_t>(other)] = kOpposite[static_cast<std::size_t>(direction)];
        timestamp_[static_cast<std::size_t>(other)] = timestamp_[static_cast<std::size_t>(node)];
        distance_[static_cast<std::size_t>(other)] = distance_[static_cast<std::size_t>(node)] + 1;
      }
    }
    ++active_head_;
  }
  // Compact the consumed prefix so repeated solves do not grow the queue unboundedly.
  active_.clear();
  active_head_ = 0;
}

void GridMaxflow::augment(std::int32_t contact_node, std::int32_t contact_direction) {
  const std::int32_t sink_side_node = neighbor(contact_node, contact_direction);
  assert(sink_side_node >= 0);

  float bottleneck = arc_cap_[static_cast<std::size_t>(contact_node) * kDirections +
                              static_cast<std::size_t>(contact_direction)];
  // Source-side residuals run parent -> child.
  for (std::int32_t node = contact_node;;) {
    const std::int8_t parent_direction = parent_[static_cast<std::size_t>(node)];
    if (parent_direction == kParentTerminal) {
      bottleneck = std::min(bottleneck, terminal_cap_[static_cast<std::size_t>(node)]);
      break;
    }
    const std::int32_t parent = neighbor(node, parent_direction);
    bottleneck = std::min(
        bottleneck, arc_cap_[static_cast<std::size_t>(parent) * kDirections +
                             static_cast<std::size_t>(kOpposite[static_cast<std::size_t>(parent_direction)])]);
    node = parent;
  }
  // Sink-side residuals run child -> parent.
  for (std::int32_t node = sink_side_node;;) {
    const std::int8_t parent_direction = parent_[static_cast<std::size_t>(node)];
    if (parent_direction == kParentTerminal) {
      bottleneck = std::min(bottleneck, -terminal_cap_[static_cast<std::size_t>(node)]);
      break;
    }
    bottleneck = std::min(bottleneck, arc_cap_[static_cast<std::size_t>(node) * kDirections +
                                               static_cast<std::size_t>(parent_direction)]);
    node = neighbor(node, parent_direction);
  }

  // Push the bottleneck along the whole path; saturated tree arcs orphan their child node.
  arc_cap_[static_cast<std::size_t>(contact_node) * kDirections +
           static_cast<std::size_t>(contact_direction)] -= bottleneck;
  arc_cap_[static_cast<std::size_t>(sink_side_node) * kDirections +
           static_cast<std::size_t>(kOpposite[static_cast<std::size_t>(contact_direction)])] += bottleneck;

  for (std::int32_t node = contact_node;;) {
    const std::int8_t parent_direction = parent_[static_cast<std::size_t>(node)];
    if (parent_direction == kParentTerminal) {
      terminal_cap_[static_cast<std::size_t>(node)] -= bottleneck;
      if (terminal_cap_[static_cast<std::size_t>(node)] <= 0.0f) {
        parent_[static_cast<std::size_t>(node)] = kParentNone;
        orphans_.push_back(node);
      }
      break;
    }
    const std::int32_t parent = neighbor(node, parent_direction);
    const std::size_t down_arc =
        static_cast<std::size_t>(parent) * kDirections +
        static_cast<std::size_t>(kOpposite[static_cast<std::size_t>(parent_direction)]);
    arc_cap_[down_arc] -= bottleneck;
    arc_cap_[static_cast<std::size_t>(node) * kDirections + static_cast<std::size_t>(parent_direction)] +=
        bottleneck;
    if (arc_cap_[down_arc] <= 0.0f) {
      parent_[static_cast<std::size_t>(node)] = kParentNone;
      orphans_.push_back(node);
    }
    node = parent;
  }
  for (std::int32_t node = sink_side_node;;) {
    const std::int8_t parent_direction = parent_[static_cast<std::size_t>(node)];
    if (parent_direction == kParentTerminal) {
      terminal_cap_[static_cast<std::size_t>(node)] += bottleneck;
      if (terminal_cap_[static_cast<std::size_t>(node)] >= 0.0f) {
        parent_[static_cast<std::size_t>(node)] = kParentNone;
        orphans_.push_back(node);
      }
      break;
    }
    const std::size_t up_arc =
        static_cast<std::size_t>(node) * kDirections + static_cast<std::size_t>(parent_direction);
    const std::int32_t parent = neighbor(node, parent_direction);
    arc_cap_[up_arc] -= bottleneck;
    arc_cap_[static_cast<std::size_t>(parent) * kDirections +
             static_cast<std::size_t>(kOpposite[static_cast<std::size_t>(parent_direction)])] += bottleneck;
    if (arc_cap_[up_arc] <= 0.0f) {
      parent_[static_cast<std::size_t>(node)] = kParentNone;
      orphans_.push_back(node);
    }
    node = parent;
  }

  flow_ += bottleneck;
}

void GridMaxflow::process_orphan(std::int32_t node) {
  const std::uint8_t tree = tree_[static_cast<std::size_t>(node)];
  std::int8_t best_direction = kParentNone;
  std::int32_t best_distance = std::numeric_limits<std::int32_t>::max();
  for (std::int32_t direction = 0; direction < kDirections; ++direction) {
    const std::int32_t other = neighbor(node, direction);
    if (other < 0 || tree_[static_cast<std::size_t>(other)] != tree) {
      continue;
    }
    const float residual =
        tree == kTreeSource
            ? arc_cap_[static_cast<std::size_t>(other) * kDirections +
                       static_cast<std::size_t>(kOpposite[static_cast<std::size_t>(direction)])]
            : arc_cap_[static_cast<std::size_t>(node) * kDirections + static_cast<std::size_t>(direction)];
    if (residual <= 0.0f) {
      continue;
    }
    if (!has_root_path(other)) {
      continue;
    }
    if (distance_[static_cast<std::size_t>(other)] < best_distance) {
      best_distance = distance_[static_cast<std::size_t>(other)];
      best_direction = static_cast<std::int8_t>(direction);
    }
  }
  if (best_direction != kParentNone) {
    parent_[static_cast<std::size_t>(node)] = best_direction;
    timestamp_[static_cast<std::size_t>(node)] = time_;
    distance_[static_cast<std::size_t>(node)] = best_distance + 1;
    return;
  }
  // No adoptive parent: the node leaves the tree; children become orphans and potential
  // former neighbors become active so the tree can regrow toward this area.
  for (std::int32_t direction = 0; direction < kDirections; ++direction) {
    const std::int32_t other = neighbor(node, direction);
    if (other < 0 || tree_[static_cast<std::size_t>(other)] != tree) {
      continue;
    }
    if (parent_[static_cast<std::size_t>(other)] == kOpposite[static_cast<std::size_t>(direction)]) {
      parent_[static_cast<std::size_t>(other)] = kParentNone;
      orphans_.push_back(other);
    }
    const float residual =
        tree == kTreeSource
            ? arc_cap_[static_cast<std::size_t>(other) * kDirections +
                       static_cast<std::size_t>(kOpposite[static_cast<std::size_t>(direction)])]
            : arc_cap_[static_cast<std::size_t>(node) * kDirections + static_cast<std::size_t>(direction)];
    if (residual > 0.0f) {
      active_.push_back(other);
    }
  }
  tree_[static_cast<std::size_t>(node)] = kTreeFree;
  parent_[static_cast<std::size_t>(node)] = kParentNone;
}

void GridMaxflow::adopt() {
  std::size_t head = 0;
  while (head < orphans_.size()) {
    const std::int32_t node = orphans_[head];
    ++head;
    // A node re-orphaned and processed already may have left its tree.
    if (tree_[static_cast<std::size_t>(node)] != kTreeFree &&
        parent_[static_cast<std::size_t>(node)] == kParentNone) {
      process_orphan(node);
    }
  }
  orphans_.clear();
}

double GridMaxflow::solve() {
  active_.clear();
  active_head_ = 0;
  orphans_.clear();
  time_ = 0;
  for (std::int32_t node = 0; node < node_count_; ++node) {
    if (terminal_cap_[static_cast<std::size_t>(node)] > 0.0f) {
      tree_[static_cast<std::size_t>(node)] = kTreeSource;
    } else if (terminal_cap_[static_cast<std::size_t>(node)] < 0.0f) {
      tree_[static_cast<std::size_t>(node)] = kTreeSink;
    } else {
      tree_[static_cast<std::size_t>(node)] = kTreeFree;
      continue;
    }
    parent_[static_cast<std::size_t>(node)] = kParentTerminal;
    timestamp_[static_cast<std::size_t>(node)] = 0;
    distance_[static_cast<std::size_t>(node)] = 1;
    active_.push_back(node);
  }
  while (true) {
    std::int32_t contact_node = -1;
    std::int32_t contact_direction = -1;
    grow(contact_node, contact_direction);
    if (contact_node < 0) {
      break;
    }
    // The growing node stays at the queue head: grow() returned mid-scan without advancing.
    ++time_;
    augment(contact_node, contact_direction);
    adopt();
  }
  return flow_;
}

}  // namespace detail

namespace {

// Everything the solver needs about the (possibly downsampled) stroke window.
struct SolveGrid {
  Rect window;       // full-resolution document rect being solved
  std::int32_t shift{0};
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<RgbaSample> color;
  std::vector<std::uint8_t> base;  // majority of full-res base selection per cell
  std::vector<std::uint8_t> seed;  // any effective seed in cell
};

}  // namespace

QuickSelectResult quick_select_segment(const std::uint8_t* rgba, std::int32_t width, std::int32_t height,
                                       std::ptrdiff_t stride_bytes, const std::uint8_t* base_selection,
                                       const std::uint8_t* seed_mask, Rect seed_bounds,
                                       const QuickSelectParams& params) {
  QuickSelectResult result;
  if (rgba == nullptr || seed_mask == nullptr || width <= 0 || height <= 0) {
    return result;
  }
  const Rect image_rect = Rect::from_size(width, height);
  const Rect clamped_seed_bounds = intersect_rect(seed_bounds, image_rect);
  if (clamped_seed_bounds.empty()) {
    return result;
  }

  const auto base_at = [&](std::int32_t x, std::int32_t y) noexcept {
    return base_selection != nullptr && base_selection[static_cast<std::size_t>(y) * width + x] != 0;
  };
  const auto footprint_at = [&](std::int32_t x, std::int32_t y) noexcept {
    return seed_mask[static_cast<std::size_t>(y) * width + x] != 0;
  };
  // Effective seeds: in add mode a seed must claim new ground; in subtract mode it must hit
  // the existing selection. Anything else is a no-op stroke pixel.
  const auto seed_at = [&](std::int32_t x, std::int32_t y) noexcept {
    return footprint_at(x, y) && (params.subtract ? base_at(x, y) : !base_at(x, y));
  };

  Rect seed_tight{0, 0, 0, 0};
  {
    std::int32_t min_x = width;
    std::int32_t min_y = height;
    std::int32_t max_x = -1;
    std::int32_t max_y = -1;
    for (std::int32_t y = clamped_seed_bounds.y; y < clamped_seed_bounds.y + clamped_seed_bounds.height; ++y) {
      for (std::int32_t x = clamped_seed_bounds.x; x < clamped_seed_bounds.x + clamped_seed_bounds.width; ++x) {
        if (seed_at(x, y)) {
          min_x = std::min(min_x, x);
          min_y = std::min(min_y, y);
          max_x = std::max(max_x, x);
          max_y = std::max(max_y, y);
        }
      }
    }
    if (max_x < min_x) {
      return result;
    }
    seed_tight = Rect{min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
  }

  // Growth budget beyond the brush footprint (Photoshop-like "spread"): measured against
  // PS 2026 with its default spread of 50, a click selects roughly 1.5-2.5x the brush
  // diameter, edge-snapped within that budget, never a color flood.
  const double growth_px =
      std::max(4.0, params.brush_radius * (std::clamp(params.spread, 0, 100) / 50.0) * 2.5 + 8.0);
  const std::int32_t padding = std::max<std::int32_t>(
      params.min_window_padding,
      static_cast<std::int32_t>(std::lround(growth_px)) + params.brush_radius * 2);
  const Rect window = intersect_rect(dilated_rect(seed_tight, padding), image_rect);

  SolveGrid grid;
  grid.window = window;
  while (grid.shift < 3) {
    const std::int64_t nodes = (static_cast<std::int64_t>(window.width + (1 << grid.shift) - 1) >> grid.shift) *
                               (static_cast<std::int64_t>(window.height + (1 << grid.shift) - 1) >> grid.shift);
    if (nodes <= kMaxSolveNodes) {
      break;
    }
    ++grid.shift;
  }
  grid.width = (window.width + (1 << grid.shift) - 1) >> grid.shift;
  grid.height = (window.height + (1 << grid.shift) - 1) >> grid.shift;
  grid.color.resize(static_cast<std::size_t>(grid.width) * grid.height);
  grid.base.assign(static_cast<std::size_t>(grid.width) * grid.height, 0);
  grid.seed.assign(static_cast<std::size_t>(grid.width) * grid.height, 0);

  for (std::int32_t cell_y = 0; cell_y < grid.height; ++cell_y) {
    for (std::int32_t cell_x = 0; cell_x < grid.width; ++cell_x) {
      const std::int32_t x0 = window.x + (cell_x << grid.shift);
      const std::int32_t y0 = window.y + (cell_y << grid.shift);
      const std::int32_t x1 = std::min<std::int32_t>(x0 + (1 << grid.shift), window.x + window.width);
      const std::int32_t y1 = std::min<std::int32_t>(y0 + (1 << grid.shift), window.y + window.height);
      std::int32_t sum_r = 0;
      std::int32_t sum_g = 0;
      std::int32_t sum_b = 0;
      std::int32_t sum_a = 0;
      std::int32_t base_count = 0;
      std::uint8_t any_seed = 0;
      for (std::int32_t y = y0; y < y1; ++y) {
        for (std::int32_t x = x0; x < x1; ++x) {
          const RgbaSample sample = premultiplied_sample(rgba, stride_bytes, x, y);
          sum_r += sample.r;
          sum_g += sample.g;
          sum_b += sample.b;
          sum_a += sample.a;
          base_count += base_at(x, y) ? 1 : 0;
          any_seed |= seed_at(x, y) ? 1 : 0;
        }
      }
      const std::int32_t area = std::max<std::int32_t>(1, (x1 - x0) * (y1 - y0));
      const std::size_t cell = static_cast<std::size_t>(cell_y) * grid.width + cell_x;
      grid.color[cell] = RgbaSample{static_cast<std::uint8_t>(sum_r / area),
                                    static_cast<std::uint8_t>(sum_g / area),
                                    static_cast<std::uint8_t>(sum_b / area),
                                    static_cast<std::uint8_t>(sum_a / area)};
      grid.base[cell] = base_count * 2 >= area ? 1 : 0;
      grid.seed[cell] = any_seed;
    }
  }

  // Denoise a COPY of the grid (3x3 box blur, twice) for the contrast term: per-pixel sensor
  // noise otherwise creates phantom contours the cut snaps to (Photoshop clearly ignores
  // noise-scale edges — its flat-area clicks return exactly the brush disc). The data term
  // keeps reading the raw colors: blurred colors smear hard boundaries and pull the cut a
  // couple of pixels inside the true edge.
  std::vector<RgbaSample> contrast_color = grid.color;
  {
    std::vector<RgbaSample> blurred(grid.color.size());
    for (int pass = 0; pass < 2; ++pass) {
      for (std::int32_t cy = 0; cy < grid.height; ++cy) {
        for (std::int32_t cx = 0; cx < grid.width; ++cx) {
          int sums[4] = {0, 0, 0, 0};
          int count = 0;
          for (int dy = -1; dy <= 1; ++dy) {
            const std::int32_t sy = std::clamp<std::int32_t>(cy + dy, 0, grid.height - 1);
            for (int dx = -1; dx <= 1; ++dx) {
              const std::int32_t sx = std::clamp<std::int32_t>(cx + dx, 0, grid.width - 1);
              const RgbaSample& s = contrast_color[static_cast<std::size_t>(sy) * grid.width + sx];
              sums[0] += s.r;
              sums[1] += s.g;
              sums[2] += s.b;
              sums[3] += s.a;
              ++count;
            }
          }
          blurred[static_cast<std::size_t>(cy) * grid.width + cx] =
              RgbaSample{static_cast<std::uint8_t>(sums[0] / count), static_cast<std::uint8_t>(sums[1] / count),
                         static_cast<std::uint8_t>(sums[2] / count), static_cast<std::uint8_t>(sums[3] / count)};
        }
      }
      contrast_color.swap(blurred);
    }
  }

  // Color models. Foreground from the (full-resolution) effective seeds; background from a
  // deterministic random sample of the pixels the stroke is competing against.
  ColorHistogram foreground;
  for (std::int32_t y = seed_tight.y; y < seed_tight.y + seed_tight.height; ++y) {
    for (std::int32_t x = seed_tight.x; x < seed_tight.x + seed_tight.width; ++x) {
      if (seed_at(x, y)) {
        foreground.add(premultiplied_sample(rgba, stride_bytes, x, y));
      }
    }
  }
  ColorHistogram background;
  {
    XorShift32 rng(params.rng_seed);
    const int wanted = std::max(0, params.background_samples);
    const auto background_candidate = [&](std::int32_t x, std::int32_t y) noexcept {
      if (footprint_at(x, y)) {
        return false;
      }
      return params.subtract ? base_at(x, y) : !base_at(x, y);
    };
    // Sample the solve window, not the whole image: the cut competes against the stroke's
    // local surroundings, and on a large photo a global sample barely represents them (the
    // undersampled local colors then all read as foreground).
    for (int attempt = 0; attempt < wanted * 16 && background.total < wanted; ++attempt) {
      const std::int32_t x =
          window.x + static_cast<std::int32_t>(rng.next() % static_cast<std::uint32_t>(window.width));
      const std::int32_t y =
          window.y + static_cast<std::int32_t>(rng.next() % static_cast<std::uint32_t>(window.height));
      if (background_candidate(x, y)) {
        background.add(premultiplied_sample(rgba, stride_bytes, x, y));
      }
    }
    // Nearly the whole window is brushed/selected: fall back to a global sample.
    for (int attempt = 0; attempt < wanted * 16 && background.total < 32; ++attempt) {
      const std::int32_t x = static_cast<std::int32_t>(rng.next() % static_cast<std::uint32_t>(width));
      const std::int32_t y = static_cast<std::int32_t>(rng.next() % static_cast<std::uint32_t>(height));
      if (background_candidate(x, y)) {
        background.add(premultiplied_sample(rgba, stride_bytes, x, y));
      }
    }
  }
  const bool neutral_data = foreground.total < 1.0 || background.total < 32.0;

  // Chamfer distance (in pixels) from the brush footprint over the solve grid, for the
  // spread-budget bias below.
  std::vector<std::int32_t> chamfer(static_cast<std::size_t>(grid.width) * grid.height,
                                    std::numeric_limits<std::int32_t>::max() / 4);
  {
    for (std::size_t cell = 0; cell < chamfer.size(); ++cell) {
      if (grid.seed[cell] != 0) {
        chamfer[cell] = 0;
      }
    }
    const auto relax = [&](std::size_t cell, std::size_t from, std::int32_t weight) {
      chamfer[cell] = std::min(chamfer[cell], chamfer[from] + weight);
    };
    for (std::int32_t y = 0; y < grid.height; ++y) {
      for (std::int32_t x = 0; x < grid.width; ++x) {
        const std::size_t cell = static_cast<std::size_t>(y) * grid.width + x;
        if (x > 0) relax(cell, cell - 1, 3);
        if (y > 0) {
          relax(cell, cell - grid.width, 3);
          if (x > 0) relax(cell, cell - grid.width - 1, 4);
          if (x + 1 < grid.width) relax(cell, cell - grid.width + 1, 4);
        }
      }
    }
    for (std::int32_t y = grid.height - 1; y >= 0; --y) {
      for (std::int32_t x = grid.width - 1; x >= 0; --x) {
        const std::size_t cell = static_cast<std::size_t>(y) * grid.width + x;
        if (x + 1 < grid.width) relax(cell, cell + 1, 3);
        if (y + 1 < grid.height) {
          relax(cell, cell + grid.width, 3);
          if (x + 1 < grid.width) relax(cell, cell + grid.width + 1, 4);
          if (x > 0) relax(cell, cell + grid.width - 1, 4);
        }
      }
    }
  }
  // Gentle ramp inside the growth budget (strong evidence may use the whole budget, like
  // Photoshop filling an eye from a small click), steep wall beyond it.
  const auto spread_bias = [&](std::size_t cell) {
    const double dist_px = chamfer[cell] * (1 << grid.shift) / 3.0;
    const double t = dist_px / growth_px;
    const double in_budget = 1.2 * std::min(t, 1.0) * std::min(t, 1.0);
    const double past_budget = t > 1.0 ? 8.0 * (t - 1.0) * (t - 1.0) : 0.0;
    return std::min(20.0, in_budget + past_budget);
  };

  // Contrast-sensitive smoothness. Deliberately a thresholded edge classifier rather than the
  // GrabCut exponential: contrast below ~2x the window's mean neighbor distance costs the same
  // as uniform (weak shading gradients and residual noise attract nothing), and only clearly
  // real edges (>~6x mean) become cheap for the cut to follow. Matches Photoshop's behavior of
  // snapping to genuine contours while a flat-area click stays brush-sized.
  double mean_pair_distance = 0.0;
  {
    double distance_sum = 0.0;
    std::int64_t pair_count = 0;
    for (std::int32_t cell_y = 0; cell_y < grid.height; ++cell_y) {
      for (std::int32_t cell_x = 0; cell_x < grid.width; ++cell_x) {
        const std::size_t cell = static_cast<std::size_t>(cell_y) * grid.width + cell_x;
        if (cell_x + 1 < grid.width) {
          distance_sum += color_distance_squared(contrast_color[cell], contrast_color[cell + 1]);
          ++pair_count;
        }
        if (cell_y + 1 < grid.height) {
          distance_sum += color_distance_squared(contrast_color[cell], contrast_color[cell + grid.width]);
          ++pair_count;
        }
      }
    }
    mean_pair_distance = pair_count > 0 ? distance_sum / static_cast<double>(pair_count) : 0.0;
  }

  detail::GridMaxflow graph(grid.width, grid.height);
  const auto neighbor_cap = [&](std::size_t cell_a, std::size_t cell_b, double scale) {
    if (params.subtract && (grid.base[cell_a] == 0 || grid.base[cell_b] == 0)) {
      return;  // subtraction is decided inside the existing selection only
    }
    const double distance = color_distance_squared(contrast_color[cell_a], contrast_color[cell_b]);
    const double ratio = distance / (mean_pair_distance + 1e-6);
    double edge_t = std::clamp((ratio - 2.0) / 4.0, 0.0, 1.0);
    edge_t = edge_t * edge_t * (3.0 - 2.0 * edge_t);  // smoothstep between 2x and 6x mean
    const float cap = static_cast<float>(params.lambda * (1.0 - 0.85 * edge_t) * scale);
    graph.set_neighbor_cap(static_cast<std::int32_t>(cell_a), static_cast<std::int32_t>(cell_b), cap);
  };
  constexpr double kInvSqrt2 = 0.7071067811865475;
  for (std::int32_t cell_y = 0; cell_y < grid.height; ++cell_y) {
    for (std::int32_t cell_x = 0; cell_x < grid.width; ++cell_x) {
      const std::size_t cell = static_cast<std::size_t>(cell_y) * grid.width + cell_x;
      if (cell_x + 1 < grid.width) {
        neighbor_cap(cell, cell + 1, 1.0);
      }
      if (cell_y + 1 < grid.height) {
        neighbor_cap(cell, cell + grid.width, 1.0);
        if (cell_x + 1 < grid.width) {
          neighbor_cap(cell, cell + grid.width + 1, kInvSqrt2);
        }
        if (cell_x > 0) {
          neighbor_cap(cell, cell + grid.width - 1, kInvSqrt2);
        }
      }
    }
  }

  // Hard background only on window borders that are interior cuts through the image; a window
  // flush with the canvas edge must not force the object away from that edge.
  const bool border_left = window.x > 0;
  const bool border_top = window.y > 0;
  const bool border_right = window.x + window.width < width;
  const bool border_bottom = window.y + window.height < height;
  const auto cell_on_hard_border = [&](std::int32_t cell_x, std::int32_t cell_y) noexcept {
    return (border_left && cell_x == 0) || (border_top && cell_y == 0) ||
           (border_right && cell_x == grid.width - 1) || (border_bottom && cell_y == grid.height - 1);
  };

  for (std::int32_t cell_y = 0; cell_y < grid.height; ++cell_y) {
    for (std::int32_t cell_x = 0; cell_x < grid.width; ++cell_x) {
      const std::size_t cell = static_cast<std::size_t>(cell_y) * grid.width + cell_x;
      const std::int32_t node = static_cast<std::int32_t>(cell);
      if (!params.subtract) {
        if (grid.seed[cell] != 0 || grid.base[cell] != 0) {
          graph.set_terminal_caps(node, kHardCap, 0.0f);
          continue;
        }
        if (cell_on_hard_border(cell_x, cell_y)) {
          graph.set_terminal_caps(node, 0.0f, kHardCap);
          continue;
        }
      } else {
        if (grid.base[cell] == 0) {
          continue;  // outside the selection: not part of the subtract problem
        }
        if (grid.seed[cell] != 0) {
          graph.set_terminal_caps(node, kHardCap, 0.0f);  // source side = removed
          continue;
        }
        if (cell_on_hard_border(cell_x, cell_y)) {
          graph.set_terminal_caps(node, 0.0f, kHardCap);  // selection continues outside: keep
          continue;
        }
      }
      if (neutral_data) {
        continue;
      }
      // Symmetric-epsilon posterior: colors in neither model land exactly at 0.5. Background
      // samples inevitably include some of the object being brushed (its extent is unknown
      // until after the solve); shared colors end up mildly ambiguous and the smoothness term
      // resolves them locally instead of the data term flooding either way.
      constexpr double kFrequencyEps = 1e-4;
      const double f_fg = foreground.frequency(grid.color[cell]);
      const double f_bg = background.frequency(grid.color[cell]);
      const double posterior_fg = (f_fg + kFrequencyEps) / (f_fg + f_bg + 2.0 * kFrequencyEps);
      const double cost_labeled_bg = std::min(kDataCostClamp, -std::log(1.0 - posterior_fg + 1e-9));
      // Unbrushed pixels carry a small constant background prior (without it, neutral regions
      // reachable from the seeds get swept onto the source side "for free") plus the spread
      // bias, a steep cost ramp past the growth budget that keeps a tap tap-sized.
      const double cost_labeled_fg =
          std::min(kDataCostClamp, -std::log(posterior_fg + 1e-9)) + kBackgroundPrior + spread_bias(cell);
      graph.set_terminal_caps(node, static_cast<float>(cost_labeled_bg),
                              static_cast<float>(cost_labeled_fg));
    }
  }

  graph.solve();

  // Finalize at full resolution: expand the labels, re-apply the hard constraints, optionally
  // smooth (Enhance Edge), then keep only the change connected to the brushed seeds.
  const std::size_t window_area = static_cast<std::size_t>(window.width) * window.height;
  std::vector<std::uint8_t> label(window_area, 0);
  std::vector<std::uint8_t> protect(window_area, 0);
  const auto pixel_on_hard_border = [&](std::int32_t x, std::int32_t y) noexcept {
    return (border_left && x == window.x) || (border_top && y == window.y) ||
           (border_right && x == window.x + window.width - 1) ||
           (border_bottom && y == window.y + window.height - 1);
  };
  for (std::int32_t y = window.y; y < window.y + window.height; ++y) {
    for (std::int32_t x = window.x; x < window.x + window.width; ++x) {
      const std::size_t local = static_cast<std::size_t>(y - window.y) * window.width + (x - window.x);
      const std::size_t cell = (static_cast<std::size_t>(y - window.y) >> grid.shift) * grid.width +
                               ((static_cast<std::size_t>(x - window.x)) >> grid.shift);
      const bool source_side = graph.is_source_side(static_cast<std::int32_t>(cell));
      const bool base = base_at(x, y);
      const bool seed = seed_at(x, y);
      if (!params.subtract) {
        bool foreground_pixel = source_side || base || seed;
        bool pinned = base || seed;
        if (!base && pixel_on_hard_border(x, y)) {
          foreground_pixel = false;
          pinned = true;
        }
        label[local] = foreground_pixel ? 255 : 0;
        protect[local] = pinned ? 1 : 0;
      } else {
        bool removed = (source_side && base) || seed;
        bool pinned = seed || !base;
        if (base && pixel_on_hard_border(x, y)) {
          removed = false;
          pinned = true;
        }
        label[local] = removed ? 255 : 0;
        protect[local] = pinned ? 1 : 0;
      }
    }
  }
  if (params.enhance_edge || grid.shift > 0) {
    // Smoothing doubles as cleanup of the blocky downsampled-solve upsample.
    majority_smooth(label.data(), window.width, window.height, Rect::from_size(window.width, window.height),
                    protect.data(), 2);
  }

  // Connectivity filter (4-connected BFS from the seeds): a stroke only ever changes one
  // contiguous area around itself; anything the cut flipped elsewhere in the window is noise.
  std::vector<std::uint8_t> reachable(window_area, 0);
  std::vector<std::int32_t> queue;
  queue.reserve(1024);
  for (std::int32_t y = seed_tight.y; y < seed_tight.y + seed_tight.height; ++y) {
    for (std::int32_t x = seed_tight.x; x < seed_tight.x + seed_tight.width; ++x) {
      if (!seed_at(x, y)) {
        continue;
      }
      const std::size_t local = static_cast<std::size_t>(y - window.y) * window.width + (x - window.x);
      if (reachable[local] == 0 && label[local] != 0) {
        reachable[local] = 1;
        queue.push_back(static_cast<std::int32_t>(local));
      }
    }
  }
  while (!queue.empty()) {
    const std::int32_t local = queue.back();
    queue.pop_back();
    const std::int32_t lx = local % window.width;
    const std::int32_t ly = local / window.width;
    const std::array<std::int32_t, 4> nx{lx + 1, lx - 1, lx, lx};
    const std::array<std::int32_t, 4> ny{ly, ly, ly + 1, ly - 1};
    for (int i = 0; i < 4; ++i) {
      if (nx[i] < 0 || nx[i] >= window.width || ny[i] < 0 || ny[i] >= window.height) {
        continue;
      }
      const std::size_t next = static_cast<std::size_t>(ny[i]) * window.width + nx[i];
      if (reachable[next] == 0 && label[next] != 0) {
        reachable[next] = 1;
        queue.push_back(static_cast<std::int32_t>(next));
      }
    }
  }

  // Extract the delta: newly selected pixels (add) or removed pixels (subtract), reachable
  // from the stroke. Base pixels are conduits for connectivity but never part of an add delta.
  std::vector<std::uint8_t> delta_flag(window_area, 0);
  for (std::int32_t y = window.y; y < window.y + window.height; ++y) {
    for (std::int32_t x = window.x; x < window.x + window.width; ++x) {
      const std::size_t local = static_cast<std::size_t>(y - window.y) * window.width + (x - window.x);
      if (reachable[local] != 0) {
        delta_flag[local] = (params.subtract ? base_at(x, y) : !base_at(x, y)) ? 1 : 0;
      }
    }
  }

  // Fill enclosed holes (Photoshop keeps catchlights inside an eye selection): any region the
  // delta itself fully surrounds joins it. The flood travels through EVERY unchanged pixel —
  // restricting it (say, to kept-base pixels in subtract mode) misclassifies a legitimately
  // kept ring around a removal as "enclosed" and deletes it. The mode filter applies only when
  // converting a confirmed hole into delta.
  {
    std::vector<std::uint8_t> outside(window_area, 0);
    queue.clear();
    for (std::int32_t y = 0; y < window.height; ++y) {
      for (std::int32_t x = 0; x < window.width; ++x) {
        if (x != 0 && x != window.width - 1 && y != 0 && y != window.height - 1) {
          continue;  // border cells only
        }
        const std::size_t local = static_cast<std::size_t>(y) * window.width + x;
        if (outside[local] == 0 && delta_flag[local] == 0) {
          outside[local] = 1;
          queue.push_back(static_cast<std::int32_t>(local));
        }
      }
    }
    while (!queue.empty()) {
      const std::int32_t local = queue.back();
      queue.pop_back();
      const std::int32_t lx = local % window.width;
      const std::int32_t ly = local / window.width;
      const std::array<std::int32_t, 4> nx{lx + 1, lx - 1, lx, lx};
      const std::array<std::int32_t, 4> ny{ly, ly, ly + 1, ly - 1};
      for (int i = 0; i < 4; ++i) {
        if (nx[i] < 0 || nx[i] >= window.width || ny[i] < 0 || ny[i] >= window.height) {
          continue;
        }
        const std::size_t next = static_cast<std::size_t>(ny[i]) * window.width + nx[i];
        if (outside[next] == 0 && delta_flag[next] == 0) {
          outside[next] = 1;
          queue.push_back(static_cast<std::int32_t>(next));
        }
      }
    }
    for (std::int32_t y = window.y; y < window.y + window.height; ++y) {
      for (std::int32_t x = window.x; x < window.x + window.width; ++x) {
        const std::size_t local = static_cast<std::size_t>(y - window.y) * window.width + (x - window.x);
        if (delta_flag[local] == 0 && outside[local] == 0 &&
            (params.subtract ? base_at(x, y) : !base_at(x, y))) {
          delta_flag[local] = 1;
        }
      }
    }
  }

  std::int32_t min_x = width;
  std::int32_t min_y = height;
  std::int32_t max_x = -1;
  std::int32_t max_y = -1;
  for (std::int32_t y = window.y; y < window.y + window.height; ++y) {
    std::int32_t run_start = -1;
    for (std::int32_t x = window.x; x <= window.x + window.width; ++x) {
      bool in_delta = false;
      if (x < window.x + window.width) {
        const std::size_t local = static_cast<std::size_t>(y - window.y) * window.width + (x - window.x);
        in_delta = delta_flag[local] != 0;
      }
      if (in_delta && run_start < 0) {
        run_start = x;
      } else if (!in_delta && run_start >= 0) {
        result.delta_runs.push_back(QuickSelectRun{y, run_start, x - 1});
        min_x = std::min(min_x, run_start);
        max_x = std::max(max_x, x - 1);
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
        run_start = -1;
      }
    }
  }
  if (result.delta_runs.empty()) {
    return result;
  }
  result.delta_bounds = Rect{min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
  result.delta_mask.assign(static_cast<std::size_t>(result.delta_bounds.width) * result.delta_bounds.height,
                           0);
  for (const QuickSelectRun& run : result.delta_runs) {
    std::uint8_t* row = result.delta_mask.data() +
                        static_cast<std::size_t>(run.y - result.delta_bounds.y) * result.delta_bounds.width;
    std::memset(row + (run.x0 - result.delta_bounds.x), 255,
                static_cast<std::size_t>(run.x1 - run.x0 + 1));
  }
  return result;
}

}  // namespace patchy
