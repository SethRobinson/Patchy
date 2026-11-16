#include "ocr/ocr_layout.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace patchy::ocr {

namespace {

constexpr double kMaxThicknessRatio = 0.7;  // thinner / thicker must be at least this
constexpr double kMaxGap = 0.9;             // of the thinner line's thickness
constexpr double kMaxOverlapAcross = 0.3;   // of the thinner line's thickness
constexpr double kMinOverlapAlong = 0.5;    // of the shorter line's length
constexpr double kMaxUprightAngle = 2.0;    // degrees

// A line measured in its block's terms: `along` runs with the text, `across` runs from one
// line to the next.
struct Extent {
  double along_start{0.0};
  double along_end{0.0};
  double across_start{0.0};
  double across_end{0.0};
  [[nodiscard]] double thickness() const noexcept { return across_end - across_start; }
  [[nodiscard]] double length() const noexcept { return along_end - along_start; }
};

Extent extent_of(const Line& line) noexcept {
  if (line.vertical) {
    return Extent{line.box.y, line.box.y + line.box.height, line.box.x, line.box.x + line.box.width};
  }
  return Extent{line.box.x, line.box.x + line.box.width, line.box.y, line.box.y + line.box.height};
}

bool upright(const Line& line) noexcept {
  return std::abs(line.angle_degrees) <= kMaxUprightAngle;
}

bool lines_join(const Line& first, const Line& second) noexcept {
  if (first.vertical != second.vertical || !upright(first) || !upright(second)) {
    return false;
  }
  const Extent a = extent_of(first);
  const Extent b = extent_of(second);
  const double thinner = std::min(a.thickness(), b.thickness());
  const double thicker = std::max(a.thickness(), b.thickness());
  if (!(thinner > 0.0) || thinner < kMaxThicknessRatio * thicker) {
    return false;
  }
  // Positive: empty space between the lines. Negative: they overlap across.
  const double gap = std::max(a.across_start, b.across_start) - std::min(a.across_end, b.across_end);
  if (gap > kMaxGap * thinner || gap < -kMaxOverlapAcross * thinner) {
    return false;
  }
  const double overlap = std::min(a.along_end, b.along_end) - std::max(a.along_start, b.along_start);
  return overlap >= kMinOverlapAlong * std::min(a.length(), b.length());
}

double median(std::vector<double> values) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const std::size_t middle = values.size() / 2;
  return values.size() % 2 == 1 ? values[middle] : (values[middle - 1] + values[middle]) / 2.0;
}

double spread(const std::vector<double>& values) {
  const auto [low, high] = std::minmax_element(values.begin(), values.end());
  return *high - *low;
}

Block finish_block(const Result& result, std::vector<std::size_t> members) {
  Block block;
  block.vertical = result.lines[members.front()].vertical;
  // Reading order inside the block; the index breaks ties so the order is total.
  std::sort(members.begin(), members.end(), [&](std::size_t left, std::size_t right) {
    const Extent a = extent_of(result.lines[left]);
    const Extent b = extent_of(result.lines[right]);
    const double key_a = block.vertical ? -(a.across_start + a.across_end) : a.across_start + a.across_end;
    const double key_b = block.vertical ? -(b.across_start + b.across_end) : b.across_start + b.across_end;
    return key_a != key_b ? key_a < key_b : left < right;
  });

  double left = 0.0;
  double top = 0.0;
  double right = 0.0;
  double bottom = 0.0;
  std::vector<double> thicknesses;
  std::vector<double> starts;
  std::vector<double> centers;
  std::vector<double> ends;
  std::vector<double> pitches;
  for (std::size_t position = 0; position < members.size(); ++position) {
    const Line& line = result.lines[members[position]];
    const Extent extent = extent_of(line);
    if (position == 0) {
      left = line.box.x;
      top = line.box.y;
      right = line.box.x + line.box.width;
      bottom = line.box.y + line.box.height;
    } else {
      left = std::min(left, line.box.x);
      top = std::min(top, line.box.y);
      right = std::max(right, line.box.x + line.box.width);
      bottom = std::max(bottom, line.box.y + line.box.height);
      const Extent previous = extent_of(result.lines[members[position - 1]]);
      pitches.push_back(std::abs((extent.across_start + extent.across_end) -
                                 (previous.across_start + previous.across_end)) /
                        2.0);
    }
    thicknesses.push_back(extent.thickness());
    starts.push_back(extent.along_start);
    centers.push_back((extent.along_start + extent.along_end) / 2.0);
    ends.push_back(extent.along_end);
  }
  block.box = Box{left, top, right - left, bottom - top};
  block.line_size = median(thicknesses);
  block.line_pitch = median(pitches);
  if (members.size() > 1) {
    // The edge the lines share most closely; Start wins ties, then Center.
    const double start_spread = spread(starts);
    const double center_spread = spread(centers);
    const double end_spread = spread(ends);
    if (center_spread < start_spread && center_spread <= end_spread) {
      block.alignment = Alignment::Center;
    } else if (end_spread < start_spread && end_spread < center_spread) {
      block.alignment = Alignment::End;
    }
  }
  block.lines = std::move(members);
  return block;
}

void sort_blocks(std::vector<Block>& blocks) {
  std::stable_sort(blocks.begin(), blocks.end(), [](const Block& a, const Block& b) {
    if (a.vertical != b.vertical) {
      return !a.vertical;
    }
    if (a.vertical) {
      const double right_a = a.box.x + a.box.width;
      const double right_b = b.box.x + b.box.width;
      return right_a != right_b ? right_a > right_b : a.box.y < b.box.y;
    }
    return a.box.y != b.box.y ? a.box.y < b.box.y : a.box.x < b.box.x;
  });
}

}  // namespace

std::vector<Block> group_lines_into_blocks(const Result& result) {
  const std::size_t count = result.lines.size();
  // Union-find over every pair that joins: a block is a connected set.
  std::vector<std::size_t> parent(count);
  std::iota(parent.begin(), parent.end(), std::size_t{0});
  const auto root = [&parent](std::size_t index) {
    while (parent[index] != index) {
      parent[index] = parent[parent[index]];
      index = parent[index];
    }
    return index;
  };
  for (std::size_t first = 0; first < count; ++first) {
    for (std::size_t second = first + 1; second < count; ++second) {
      if (lines_join(result.lines[first], result.lines[second])) {
        const std::size_t a = root(first);
        const std::size_t b = root(second);
        if (a != b) {
          parent[std::max(a, b)] = std::min(a, b);
        }
      }
    }
  }
  std::vector<std::vector<std::size_t>> members(count);
  for (std::size_t index = 0; index < count; ++index) {
    members[root(index)].push_back(index);
  }
  std::vector<Block> blocks;
  for (auto& group : members) {
    if (!group.empty()) {
      blocks.push_back(finish_block(result, std::move(group)));
    }
  }
  sort_blocks(blocks);
  return blocks;
}

std::vector<Block> lines_as_blocks(const Result& result) {
  std::vector<Block> blocks;
  blocks.reserve(result.lines.size());
  for (std::size_t index = 0; index < result.lines.size(); ++index) {
    blocks.push_back(finish_block(result, {index}));
  }
  sort_blocks(blocks);
  return blocks;
}

}  // namespace patchy::ocr
