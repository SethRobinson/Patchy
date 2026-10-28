#include "render/tile_cache.hpp"

#include "support/translate_noop.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>

namespace patchy {
namespace {

Rect tile_bounds(TileKey key, std::int32_t tile_size) {
  if (key.mip < 0 || tile_size <= 0 || key.mip > 30) {
    return {};
  }
  const auto scale = static_cast<std::int64_t>(1) << key.mip;
  const auto scaled_size = static_cast<std::int64_t>(tile_size) * scale;
  const auto left = static_cast<std::int64_t>(key.x) * scaled_size;
  const auto top = static_cast<std::int64_t>(key.y) * scaled_size;
  if (left < std::numeric_limits<std::int32_t>::min() || left > std::numeric_limits<std::int32_t>::max() ||
      top < std::numeric_limits<std::int32_t>::min() || top > std::numeric_limits<std::int32_t>::max() ||
      scaled_size > std::numeric_limits<std::int32_t>::max()) {
    return {};
  }
  return Rect{static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
              static_cast<std::int32_t>(scaled_size), static_cast<std::int32_t>(scaled_size)};
}

bool intersects(Rect first, Rect second) {
  if (first.empty() || second.empty()) {
    return false;
  }
  const auto first_right = static_cast<std::int64_t>(first.x) + first.width;
  const auto first_bottom = static_cast<std::int64_t>(first.y) + first.height;
  const auto second_right = static_cast<std::int64_t>(second.x) + second.width;
  const auto second_bottom = static_cast<std::int64_t>(second.y) + second.height;
  return static_cast<std::int64_t>(first.x) < second_right &&
         static_cast<std::int64_t>(second.x) < first_right &&
         static_cast<std::int64_t>(first.y) < second_bottom &&
         static_cast<std::int64_t>(second.y) < first_bottom;
}

}  // namespace

bool TileKey::operator==(const TileKey& other) const noexcept {
  return x == other.x && y == other.y && mip == other.mip;
}

std::size_t TileKeyHash::operator()(const TileKey& key) const noexcept {
  auto seed = std::hash<std::int32_t>{}(key.x);
  seed ^= std::hash<std::int32_t>{}(key.y) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
  seed ^= std::hash<std::int32_t>{}(key.mip) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
  return seed;
}

TileCache::TileCache(std::int32_t tile_size) : tile_size_(tile_size) {
  if (tile_size <= 0) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Tile size must be positive"));
  }
}

std::int32_t TileCache::tile_size() const noexcept {
  return tile_size_;
}

std::size_t TileCache::size() const noexcept {
  return tiles_.size();
}

std::optional<PixelBuffer> TileCache::find(TileKey key) const {
  const auto found = tiles_.find(key);
  if (found == tiles_.end()) {
    return std::nullopt;
  }
  return found->second;
}

void TileCache::put(TileKey key, PixelBuffer tile) {
  tiles_[key] = std::move(tile);
}

void TileCache::invalidate(TileKey key) {
  tiles_.erase(key);
}

std::size_t TileCache::invalidate_region(Rect region, std::int32_t mip) {
  if (region.empty()) {
    return 0;
  }
  std::size_t removed = 0;
  for (auto iterator = tiles_.begin(); iterator != tiles_.end();) {
    if (iterator->first.mip == mip && intersects(tile_bounds(iterator->first, tile_size_), region)) {
      iterator = tiles_.erase(iterator);
      ++removed;
    } else {
      ++iterator;
    }
  }
  return removed;
}

std::size_t TileCache::invalidate_all_mips(Rect region) {
  if (region.empty()) {
    return 0;
  }
  std::size_t removed = 0;
  for (auto iterator = tiles_.begin(); iterator != tiles_.end();) {
    if (intersects(tile_bounds(iterator->first, tile_size_), region)) {
      iterator = tiles_.erase(iterator);
      ++removed;
    } else {
      ++iterator;
    }
  }
  return removed;
}

void TileCache::clear() {
  tiles_.clear();
}

}  // namespace patchy
