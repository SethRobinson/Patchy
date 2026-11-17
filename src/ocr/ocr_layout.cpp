#include "ocr/ocr_layout.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>
#include <utility>

namespace patchy::ocr {

namespace {

// Two lines are the same size when their lengths per character are within 30% of each
// other. Thickness cannot say it alone: a line box hugs the ink, so a line with no
// descenders is a fifth thinner than its neighbour and one of x-height letters only
// ("we saw a car") nearly half, at the very same size. Thickness is still a bound (a
// heading over body text differs by more), and it decides for lines too short to average.
constexpr double kMinAdvanceRatio = 0.7;
constexpr double kMinThicknessRatioWithAdvance = 0.45;
constexpr double kMinThicknessRatioAlone = 0.7;
constexpr std::size_t kMinCharactersForAdvance = 4;
constexpr double kMaxPitch = 2.2;           // centre to centre, of the thicker line's thickness
constexpr double kMinPitch = 0.5;           // of the thinner line's thickness: not the same row twice
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

// Characters of UTF-8 text, spaces included (they take room on the line too).
std::size_t character_count(const std::string& text) noexcept {
  std::size_t count = 0;
  for (const char byte : text) {
    count += (static_cast<unsigned char>(byte) & 0xC0U) != 0x80U ? 1U : 0U;
  }
  return count;
}

// The line's length per character, or 0 when it has too few characters to say.
double advance_of(const Line& line, const Extent& extent) noexcept {
  const std::size_t characters = character_count(line.text);
  return characters >= kMinCharactersForAdvance ? extent.length() / static_cast<double>(characters) : 0.0;
}

bool lines_join(const Line& first, const Line& second) noexcept {
  if (first.vertical != second.vertical || !upright(first) || !upright(second)) {
    return false;
  }
  const Extent a = extent_of(first);
  const Extent b = extent_of(second);
  const double thinner = std::min(a.thickness(), b.thickness());
  const double thicker = std::max(a.thickness(), b.thickness());
  if (!(thinner > 0.0)) {
    return false;
  }
  const double advance_a = advance_of(first, a);
  const double advance_b = advance_of(second, b);
  if (advance_a > 0.0 && advance_b > 0.0) {
    if (std::min(advance_a, advance_b) < kMinAdvanceRatio * std::max(advance_a, advance_b) ||
        thinner < kMinThicknessRatioWithAdvance * thicker) {
      return false;
    }
  } else if (thinner < kMinThicknessRatioAlone * thicker) {
    return false;
  }
  // The pitch, not the gap: an engine's line box hugs the ink, so the empty space between
  // two lines swings with which letters they hold (a line with no descenders leaves half
  // again as much), while the distance between their centres barely moves.
  const double pitch = std::abs((a.across_start + a.across_end) - (b.across_start + b.across_end)) / 2.0;
  if (pitch > kMaxPitch * thicker || pitch < kMinPitch * thinner) {
    return false;
  }
  const double overlap = std::min(a.along_end, b.along_end) - std::max(a.along_start, b.along_start);
  return overlap >= kMinOverlapAlong * std::min(a.length(), b.length());
}

// Two lines side by side on one row: across the lines their boxes overlap by at least half
// the thinner one. A block is a stack of rows, so it can hold only one of them.
bool lines_share_row(const Line& first, const Line& second) noexcept {
  if (first.vertical != second.vertical) {
    return false;
  }
  const Extent a = extent_of(first);
  const Extent b = extent_of(second);
  const double thinner = std::min(a.thickness(), b.thickness());
  const double overlap = std::min(a.across_end, b.across_end) - std::max(a.across_start, b.across_start);
  return thinner > 0.0 && overlap >= kMinPitch * thinner;
}

// Whether two ascending index lists share an entry.
bool lists_intersect(const std::vector<std::size_t>& a, const std::vector<std::size_t>& b) noexcept {
  auto left = a.begin();
  auto right = b.begin();
  while (left != a.end() && right != b.end()) {
    if (*left == *right) {
      return true;
    }
    if (*left < *right) {
      ++left;
    } else {
      ++right;
    }
  }
  return false;
}

// Connected sets over `count` items joined by `pairs`: each set ascending, sets in the order
// of their smallest item.
std::vector<std::vector<std::size_t>> connected_sets(std::size_t count,
                                                     const std::vector<std::pair<std::size_t, std::size_t>>& pairs) {
  std::vector<std::size_t> parent(count);
  std::iota(parent.begin(), parent.end(), std::size_t{0});
  const auto root = [&parent](std::size_t index) {
    while (parent[index] != index) {
      parent[index] = parent[parent[index]];
      index = parent[index];
    }
    return index;
  };
  for (const auto& [first, second] : pairs) {
    const std::size_t a = root(first);
    const std::size_t b = root(second);
    if (a != b) {
      parent[std::max(a, b)] = std::min(a, b);
    }
  }
  std::vector<std::vector<std::size_t>> members(count);
  for (std::size_t index = 0; index < count; ++index) {
    members[root(index)].push_back(index);
  }
  std::erase_if(members, [](const std::vector<std::size_t>& set) { return set.empty(); });
  return members;
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
  // Every pair of lines is asked two things: do they join, and do they share a row.
  std::vector<std::pair<std::size_t, std::size_t>> joins;
  std::vector<std::vector<std::size_t>> joined(count);
  std::vector<std::vector<std::size_t>> row_mates(count);
  for (std::size_t first = 0; first < count; ++first) {
    for (std::size_t second = first + 1; second < count; ++second) {
      if (lines_join(result.lines[first], result.lines[second])) {
        joins.emplace_back(first, second);
      }
      if (lines_share_row(result.lines[first], result.lines[second])) {
        row_mates[first].push_back(second);
        row_mates[second].push_back(first);
      }
    }
  }
  for (auto& mates : row_mates) {
    std::sort(mates.begin(), mates.end());
  }
  for (const auto& [first, second] : joins) {
    joined[first].push_back(second);
    joined[second].push_back(first);
  }
  for (auto& list : joined) {
    std::sort(list.begin(), list.end());
  }

  // A block is a stack of rows, one line each. When two lines of one row would both join the
  // same line (a title and the labels beside it over a paragraph, two columns under a line
  // that spans them), two boxes cannot say which of them continues that line's block, so
  // neither join is made. Without this the row's lines became separate lines of one text
  // layer, whose leading (the median distance between consecutive lines) then fell to
  // nothing and drew the whole block on top of itself.
  std::erase_if(joins, [&](const std::pair<std::size_t, std::size_t>& pair) {
    return lists_intersect(row_mates[pair.first], joined[pair.second]) ||
           lists_intersect(row_mates[pair.second], joined[pair.first]);
  });

  std::vector<Block> blocks;
  for (auto& set : connected_sets(count, joins)) {
    // Row neighbours can still meet through longer chains. Then each of them stands alone,
    // and what is left of the set is joined again without them.
    std::vector<std::size_t> crowded;
    for (const std::size_t member : set) {
      if (lists_intersect(row_mates[member], set)) {
        crowded.push_back(member);
      }
    }
    if (crowded.empty()) {
      blocks.push_back(finish_block(result, std::move(set)));
      continue;
    }
    std::vector<std::size_t> rest;
    std::set_difference(set.begin(), set.end(), crowded.begin(), crowded.end(), std::back_inserter(rest));
    std::vector<std::pair<std::size_t, std::size_t>> rest_joins;
    for (const auto& [first, second] : joins) {
      const auto a = std::lower_bound(rest.begin(), rest.end(), first);
      const auto b = std::lower_bound(rest.begin(), rest.end(), second);
      if (a != rest.end() && *a == first && b != rest.end() && *b == second) {
        rest_joins.emplace_back(static_cast<std::size_t>(a - rest.begin()), static_cast<std::size_t>(b - rest.begin()));
      }
    }
    for (const auto& positions : connected_sets(rest.size(), rest_joins)) {
      std::vector<std::size_t> members;
      members.reserve(positions.size());
      for (const std::size_t position : positions) {
        members.push_back(rest[position]);
      }
      blocks.push_back(finish_block(result, std::move(members)));
    }
    for (const std::size_t member : crowded) {
      blocks.push_back(finish_block(result, {member}));
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
