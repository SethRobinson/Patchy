#include "render/tile_cache.hpp"

#include <functional>
#include <stdexcept>
#include <utility>

namespace photoslop {

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
    throw std::invalid_argument("Tile size must be positive");
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

void TileCache::clear() {
  tiles_.clear();
}

}  // namespace photoslop
