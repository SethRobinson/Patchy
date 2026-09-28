#include "core/layer_alignment.hpp"

#include <algorithm>
#include <cmath>

namespace patchy {

namespace {

double feature_position(Rect rect, AlignEdge edge) noexcept {
  switch (edge) {
    case AlignEdge::Left:
      return static_cast<double>(rect.x);
    case AlignEdge::HorizontalCenter:
      return static_cast<double>(rect.x) + static_cast<double>(rect.width) / 2.0;
    case AlignEdge::Right:
      return static_cast<double>(rect.x) + static_cast<double>(rect.width);
    case AlignEdge::Top:
      return static_cast<double>(rect.y);
    case AlignEdge::VerticalCenter:
      return static_cast<double>(rect.y) + static_cast<double>(rect.height) / 2.0;
    case AlignEdge::Bottom:
      return static_cast<double>(rect.y) + static_cast<double>(rect.height);
  }
  return 0.0;
}

std::int32_t rounded_delta(double target, double source) noexcept {
  return static_cast<std::int32_t>(std::lround(target - source));
}

AlignEdge feature_edge_for_mode(DistributeMode mode) noexcept {
  switch (mode) {
    case DistributeMode::Left:
    case DistributeMode::HorizontalSpacing:
      return AlignEdge::Left;
    case DistributeMode::HorizontalCenter:
      return AlignEdge::HorizontalCenter;
    case DistributeMode::Right:
      return AlignEdge::Right;
    case DistributeMode::Top:
    case DistributeMode::VerticalSpacing:
      return AlignEdge::Top;
    case DistributeMode::VerticalCenter:
      return AlignEdge::VerticalCenter;
    case DistributeMode::Bottom:
      return AlignEdge::Bottom;
  }
  return AlignEdge::Left;
}

}  // namespace

bool align_edge_is_horizontal(AlignEdge edge) noexcept {
  return edge == AlignEdge::Left || edge == AlignEdge::HorizontalCenter || edge == AlignEdge::Right;
}

bool distribute_mode_is_horizontal(DistributeMode mode) noexcept {
  return mode == DistributeMode::Left || mode == DistributeMode::HorizontalCenter ||
         mode == DistributeMode::Right || mode == DistributeMode::HorizontalSpacing;
}

std::string_view align_edge_id(AlignEdge edge) noexcept {
  switch (edge) {
    case AlignEdge::Left:
      return "left";
    case AlignEdge::HorizontalCenter:
      return "hcenter";
    case AlignEdge::Right:
      return "right";
    case AlignEdge::Top:
      return "top";
    case AlignEdge::VerticalCenter:
      return "vcenter";
    case AlignEdge::Bottom:
      return "bottom";
  }
  return "left";
}

std::string_view distribute_mode_id(DistributeMode mode) noexcept {
  switch (mode) {
    case DistributeMode::Left:
      return "left";
    case DistributeMode::HorizontalCenter:
      return "hcenter";
    case DistributeMode::Right:
      return "right";
    case DistributeMode::Top:
      return "top";
    case DistributeMode::VerticalCenter:
      return "vcenter";
    case DistributeMode::Bottom:
      return "bottom";
    case DistributeMode::HorizontalSpacing:
      return "hspacing";
    case DistributeMode::VerticalSpacing:
      return "vspacing";
  }
  return "left";
}

std::optional<AlignEdge> align_edge_from_id(std::string_view id) noexcept {
  for (const auto edge : {AlignEdge::Left, AlignEdge::HorizontalCenter, AlignEdge::Right, AlignEdge::Top,
                          AlignEdge::VerticalCenter, AlignEdge::Bottom}) {
    if (align_edge_id(edge) == id) {
      return edge;
    }
  }
  return std::nullopt;
}

std::optional<DistributeMode> distribute_mode_from_id(std::string_view id) noexcept {
  for (const auto mode : {DistributeMode::Left, DistributeMode::HorizontalCenter, DistributeMode::Right,
                          DistributeMode::Top, DistributeMode::VerticalCenter, DistributeMode::Bottom,
                          DistributeMode::HorizontalSpacing, DistributeMode::VerticalSpacing}) {
    if (distribute_mode_id(mode) == id) {
      return mode;
    }
  }
  return std::nullopt;
}

std::vector<AlignmentOffset> compute_align_deltas(const std::vector<Rect>& units, Rect reference, AlignEdge edge) {
  std::vector<AlignmentOffset> deltas(units.size());
  if (reference.empty()) {
    return deltas;
  }
  const auto target = feature_position(reference, edge);
  const bool horizontal = align_edge_is_horizontal(edge);
  for (std::size_t i = 0; i < units.size(); ++i) {
    if (units[i].empty()) {
      continue;
    }
    const auto delta = rounded_delta(target, feature_position(units[i], edge));
    if (horizontal) {
      deltas[i].dx = delta;
    } else {
      deltas[i].dy = delta;
    }
  }
  return deltas;
}

std::vector<AlignmentOffset> compute_distribute_deltas(const std::vector<Rect>& units, DistributeMode mode) {
  std::vector<AlignmentOffset> deltas(units.size());
  std::vector<std::size_t> order;
  order.reserve(units.size());
  for (std::size_t i = 0; i < units.size(); ++i) {
    if (!units[i].empty()) {
      order.push_back(i);
    }
  }
  if (order.size() < 3) {
    return deltas;
  }
  const auto edge = feature_edge_for_mode(mode);
  const bool horizontal = distribute_mode_is_horizontal(mode);
  // Stable by input order so equal features keep a deterministic sequence.
  std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    return feature_position(units[a], edge) < feature_position(units[b], edge);
  });
  const auto n = order.size();
  const auto assign = [&](std::size_t unit, double target) {
    const auto delta = rounded_delta(target, feature_position(units[unit], edge));
    if (horizontal) {
      deltas[unit].dx = delta;
    } else {
      deltas[unit].dy = delta;
    }
  };

  if (mode == DistributeMode::HorizontalSpacing || mode == DistributeMode::VerticalSpacing) {
    const auto extent = [horizontal](Rect rect) {
      return static_cast<double>(horizontal ? rect.width : rect.height);
    };
    const auto first = units[order.front()];
    const auto last = units[order.back()];
    const auto span = feature_position(last, edge) + extent(last) - feature_position(first, edge);
    double total = 0.0;
    for (const auto unit : order) {
      total += extent(units[unit]);
    }
    const auto gap = (span - total) / static_cast<double>(n - 1);
    auto cursor = feature_position(first, edge);
    for (std::size_t i = 0; i < n; ++i) {
      const auto unit = order[i];
      // The outermost units anchor the layout and never move.
      if (i != 0 && i + 1 != n) {
        assign(unit, cursor);
      }
      cursor += extent(units[unit]) + gap;
    }
    return deltas;
  }

  const auto first = feature_position(units[order.front()], edge);
  const auto last = feature_position(units[order.back()], edge);
  for (std::size_t i = 1; i + 1 < n; ++i) {
    const auto target = first + (last - first) * static_cast<double>(i) / static_cast<double>(n - 1);
    assign(order[i], target);
  }
  return deltas;
}

}  // namespace patchy
