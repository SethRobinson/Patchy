#include "core/style_contour.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace patchy {

namespace {

struct ContourNode {
  double x{0.0};
  double y{0.0};
  bool corner{false};
};

// Clamps, sorts by input, and resolves duplicate inputs in favor of the last
// supplied point (the Curves normalization rule).
std::vector<ContourNode> normalized_contour_nodes(const StyleContour& contour) {
  std::vector<ContourNode> nodes;
  nodes.reserve(contour.points.size());
  for (const auto& point : contour.points) {
    ContourNode node;
    node.x = std::clamp(static_cast<double>(point.x), 0.0, 255.0);
    node.y = std::clamp(static_cast<double>(point.y), 0.0, 255.0);
    node.corner = point.corner;
    nodes.push_back(node);
  }
  std::stable_sort(nodes.begin(), nodes.end(),
                   [](const ContourNode& lhs, const ContourNode& rhs) { return lhs.x < rhs.x; });
  std::vector<ContourNode> unique_nodes;
  unique_nodes.reserve(nodes.size());
  for (const auto& node : nodes) {
    if (!unique_nodes.empty() && std::abs(unique_nodes.back().x - node.x) < 1e-9) {
      unique_nodes.back() = node;
      continue;
    }
    unique_nodes.push_back(node);
  }
  return unique_nodes;
}

// Fills lut entries for inputs covered by nodes[first..last] with a natural
// cubic through those nodes (zero second derivative at the run's endpoints);
// a two-node run degenerates to the straight segment between them.
void fill_lut_for_run(std::array<std::uint8_t, 256>& lut, const std::vector<ContourNode>& nodes,
                      std::size_t first, std::size_t last) {
  const auto count = last - first + 1U;
  std::vector<double> second_derivatives(count, 0.0);
  std::vector<double> workspace(count, 0.0);
  for (std::size_t index = 1U; index + 1U < count; ++index) {
    const auto& previous = nodes[first + index - 1U];
    const auto& current = nodes[first + index];
    const auto& next = nodes[first + index + 1U];
    const auto previous_span = current.x - previous.x;
    const auto next_span = next.x - current.x;
    const auto combined_span = previous_span + next_span;
    const auto sigma = previous_span / combined_span;
    const auto pivot = sigma * second_derivatives[index - 1U] + 2.0;
    second_derivatives[index] = (sigma - 1.0) / pivot;
    const auto previous_slope = (current.y - previous.y) / previous_span;
    const auto next_slope = (next.y - current.y) / next_span;
    workspace[index] =
        (6.0 * (next_slope - previous_slope) / combined_span - sigma * workspace[index - 1U]) / pivot;
  }
  for (std::size_t upper = count - 1U; upper > 0U; --upper) {
    const auto index = upper - 1U;
    second_derivatives[index] = second_derivatives[index] * second_derivatives[upper] + workspace[index];
  }

  const auto begin_input = static_cast<int>(std::ceil(nodes[first].x - 1e-9));
  const auto end_input = static_cast<int>(std::floor(nodes[last].x + 1e-9));
  std::size_t upper = 1U;
  for (int input = std::max(0, begin_input); input <= std::min(255, end_input); ++input) {
    const auto position = static_cast<double>(input);
    while (upper + 1U < count && position > nodes[first + upper].x) {
      ++upper;
    }
    const auto& left = nodes[first + upper - 1U];
    const auto& right = nodes[first + upper];
    const auto span = right.x - left.x;
    double output = left.y;
    if (span > 1e-9) {
      const auto left_weight = (right.x - position) / span;
      const auto right_weight = (position - left.x) / span;
      output = left_weight * left.y + right_weight * right.y +
               ((left_weight * left_weight * left_weight - left_weight) * second_derivatives[upper - 1U] +
                (right_weight * right_weight * right_weight - right_weight) * second_derivatives[upper]) *
                   span * span / 6.0;
    }
    lut[static_cast<std::size_t>(input)] =
        static_cast<std::uint8_t>(std::clamp(std::lround(output), 0L, 255L));
  }
}

}  // namespace

bool style_contour_is_linear(const StyleContour& contour) noexcept {
  if (contour.points.empty()) {
    return true;
  }
  if (contour.points.size() != 2U) {
    return false;
  }
  const auto matches = [](const StyleContourPoint& point, float x, float y) {
    return std::abs(point.x - x) < 1e-4F && std::abs(point.y - y) < 1e-4F;
  };
  return matches(contour.points.front(), 0.0F, 0.0F) && matches(contour.points.back(), 255.0F, 255.0F);
}

std::array<std::uint8_t, 256> build_style_contour_lut(const StyleContour& contour) {
  std::array<std::uint8_t, 256> lut{};
  const auto nodes = normalized_contour_nodes(contour);
  if (nodes.size() < 2U) {
    for (int input = 0; input < 256; ++input) {
      lut[static_cast<std::size_t>(input)] = static_cast<std::uint8_t>(input);
    }
    return lut;
  }

  // Clamp outside the movable endpoints, like Curves.
  const auto front_output =
      static_cast<std::uint8_t>(std::clamp(std::lround(nodes.front().y), 0L, 255L));
  const auto back_output = static_cast<std::uint8_t>(std::clamp(std::lround(nodes.back().y), 0L, 255L));
  for (int input = 0; input < 256; ++input) {
    if (static_cast<double>(input) <= nodes.front().x) {
      lut[static_cast<std::size_t>(input)] = front_output;
    } else if (static_cast<double>(input) >= nodes.back().x) {
      lut[static_cast<std::size_t>(input)] = back_output;
    }
  }

  // Corner points split the curve into independent smooth runs.
  std::size_t run_start = 0U;
  for (std::size_t index = 1U; index < nodes.size(); ++index) {
    const auto is_last = index + 1U == nodes.size();
    if (nodes[index].corner || is_last) {
      fill_lut_for_run(lut, nodes, run_start, index);
      run_start = index;
    }
  }
  return lut;
}

float sample_style_contour_lut(const std::array<std::uint8_t, 256>& lut, float t,
                               bool anti_aliased) noexcept {
  const auto clamped = std::clamp(t, 0.0F, 1.0F);
  if (!anti_aliased) {
    const auto index = static_cast<std::size_t>(std::lround(clamped * 255.0F));
    return static_cast<float>(lut[index]) / 255.0F;
  }
  const auto scaled = clamped * 255.0F;
  const auto low = static_cast<std::size_t>(scaled);
  const auto high = std::min<std::size_t>(low + 1U, 255U);
  const auto fraction = scaled - static_cast<float>(low);
  const auto value =
      static_cast<float>(lut[low]) * (1.0F - fraction) + static_cast<float>(lut[high]) * fraction;
  return value / 255.0F;
}

}  // namespace patchy
