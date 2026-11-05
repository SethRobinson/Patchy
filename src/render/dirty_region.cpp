#include "render/dirty_region.hpp"

#include <algorithm>
#include <cstdint>

namespace patchy {
namespace {

bool overlaps_or_touches(const Rect& first, const Rect& second) {
  const auto first_right = static_cast<std::int64_t>(first.x) + first.width;
  const auto first_bottom = static_cast<std::int64_t>(first.y) + first.height;
  const auto second_right = static_cast<std::int64_t>(second.x) + second.width;
  const auto second_bottom = static_cast<std::int64_t>(second.y) + second.height;
  return static_cast<std::int64_t>(first.x) <= second_right &&
         static_cast<std::int64_t>(second.x) <= first_right &&
         static_cast<std::int64_t>(first.y) <= second_bottom &&
         static_cast<std::int64_t>(second.y) <= first_bottom;
}

Rect united(const Rect& first, const Rect& second) {
  const auto left = std::min(first.x, second.x);
  const auto top = std::min(first.y, second.y);
  const auto right = std::max(static_cast<std::int64_t>(first.x) + first.width,
                              static_cast<std::int64_t>(second.x) + second.width);
  const auto bottom = std::max(static_cast<std::int64_t>(first.y) + first.height,
                               static_cast<std::int64_t>(second.y) + second.height);
  return Rect{left, top, static_cast<std::int32_t>(right - left), static_cast<std::int32_t>(bottom - top)};
}

}  // namespace

void DirtyRegionSet::add(Rect bounds, std::int32_t mip) {
  add(DirtyRegion{bounds, mip});
}

void DirtyRegionSet::add(const DirtyRegion& region) {
  if (region.bounds.empty()) {
    return;
  }

  DirtyRegion merged = region;
  bool changed = true;
  while (changed) {
    changed = false;
    for (auto iterator = regions_.begin(); iterator != regions_.end();) {
      if (iterator->mip != merged.mip || !overlaps_or_touches(iterator->bounds, merged.bounds)) {
        ++iterator;
        continue;
      }
      merged.bounds = united(iterator->bounds, merged.bounds);
      iterator = regions_.erase(iterator);
      changed = true;
    }
  }
  regions_.push_back(merged);
  std::sort(regions_.begin(), regions_.end(), [](const DirtyRegion& first, const DirtyRegion& second) {
    if (first.mip != second.mip) {
      return first.mip < second.mip;
    }
    if (first.bounds.y != second.bounds.y) {
      return first.bounds.y < second.bounds.y;
    }
    return first.bounds.x < second.bounds.x;
  });
}

bool DirtyRegionSet::empty() const noexcept {
  return regions_.empty();
}

std::size_t DirtyRegionSet::size() const noexcept {
  return regions_.size();
}

const std::vector<DirtyRegion>& DirtyRegionSet::regions() const noexcept {
  return regions_;
}

void DirtyRegionSet::clear() noexcept {
  regions_.clear();
}

}  // namespace patchy
