#pragma once

// Per-pixel state of one paint stroke (opacity caps, accumulated alpha, the
// Wet Edges footprint), keyed by document pixel. Brush strokes look a pixel up
// for every pixel of every dab, so this is a sparse grid of dense 64x64 tiles
// with a one-tile cache rather than a hash map per pixel: an unordered_map
// lookup per pixel made a 300 px brush cost about 0.1 ms per document pixel of
// travel, which stalled the GUI thread between mouse moves (GitHub issue 165).
// Same semantics as the map it replaces: absent until written, a fresh entry
// starts at T{}.

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace patchy::ui {

template <typename T>
class StrokePixelMap {
 public:
  // The entry at (x, y), or nullptr when the stroke never wrote it.
  [[nodiscard]] T* find(std::int32_t x, std::int32_t y) noexcept {
    auto* tile = tile_at(x, y, false);
    if (tile == nullptr) {
      return nullptr;
    }
    const auto index = index_in_tile(x, y);
    return tile->present[index] ? &tile->values[index] : nullptr;
  }

  // The entry at (x, y), created as T{} when absent (unordered_map::operator[]).
  T& operator()(std::int32_t x, std::int32_t y) {
    auto* tile = tile_at(x, y, true);
    const auto index = index_in_tile(x, y);
    if (!tile->present[index]) {
      tile->present[index] = true;
      tile->values[index] = T{};
    }
    return tile->values[index];
  }

  // Marks (x, y) present; true when it was absent (unordered_set::insert().second).
  bool insert(std::int32_t x, std::int32_t y) {
    auto* tile = tile_at(x, y, true);
    const auto index = index_in_tile(x, y);
    if (tile->present[index]) {
      return false;
    }
    tile->present[index] = true;
    tile->values[index] = T{};
    return true;
  }

  void clear() noexcept {
    tiles_.clear();
    cached_tile_ = nullptr;
  }

  // Creates every tile under columns [x0, x1) of rows [y0, y1) without making any entry
  // present, so Cursors can reach them.
  void reserve_tiles(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
    if (x0 >= x1 || y0 >= y1) {
      return;
    }
    for (auto tile_y = y0 >> kTileShift; tile_y <= (y1 - 1) >> kTileShift; ++tile_y) {
      for (auto tile_x = x0 >> kTileShift; tile_x <= (x1 - 1) >> kTileShift; ++tile_x) {
        const auto key = tile_key(tile_x * kTileSize, tile_y * kTileSize);
        if (tiles_.find(key) == tiles_.end()) {
          tiles_.emplace(key, std::make_unique<Tile>());
        }
      }
    }
  }

 private:
  struct Tile;

 public:
  // One worker's view of reserved tiles (the parallel Clone stamp, GitHub issue 597): its
  // own one-tile cache and no tile creation. Several may run at once while nothing else
  // touches the map, as long as no two reach the same tile: give each worker whole 64-row
  // bands (y >> 6).
  class Cursor {
   public:
    explicit Cursor(StrokePixelMap& map) noexcept : map_(&map) {}

    // operator() of the map, for an entry whose tile reserve_tiles created.
    T& operator()(std::int32_t x, std::int32_t y) {
      const auto key = tile_key(x, y);
      if (tile_ == nullptr || key_ != key) {
        tile_ = map_->tiles_.at(key).get();
        key_ = key;
      }
      const auto index = index_in_tile(x, y);
      if (!tile_->present[index]) {
        tile_->present[index] = true;
        tile_->values[index] = T{};
      }
      return tile_->values[index];
    }

   private:
    StrokePixelMap* map_;
    std::uint64_t key_{0};
    Tile* tile_{nullptr};
  };

  // A Cursor's band is y >> kTileRowsShift: one row of tiles.
  static constexpr int kTileRowsShift = 6;

  [[nodiscard]] bool empty() const noexcept { return tiles_.empty(); }

 private:
  static constexpr int kTileShift = 6;
  static_assert(kTileShift == kTileRowsShift);
  static constexpr int kTileSize = 1 << kTileShift;
  static constexpr std::size_t kTilePixels = static_cast<std::size_t>(kTileSize) * kTileSize;

  struct Tile {
    std::array<T, kTilePixels> values{};
    std::bitset<kTilePixels> present{};
  };

  static std::size_t index_in_tile(std::int32_t x, std::int32_t y) noexcept {
    return static_cast<std::size_t>(y & (kTileSize - 1)) * kTileSize + static_cast<std::size_t>(x & (kTileSize - 1));
  }

  // Arithmetic shifts floor negative coordinates, so strokes that wander off
  // the canvas get their own tiles.
  static std::uint64_t tile_key(std::int32_t x, std::int32_t y) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(y >> kTileShift)) << 32U) |
           static_cast<std::uint32_t>(x >> kTileShift);
  }

  Tile* tile_at(std::int32_t x, std::int32_t y, bool create) {
    const auto key = tile_key(x, y);
    if (cached_tile_ != nullptr && cached_key_ == key) {
      return cached_tile_;
    }
    auto found = tiles_.find(key);
    if (found == tiles_.end()) {
      if (!create) {
        return nullptr;
      }
      found = tiles_.emplace(key, std::make_unique<Tile>()).first;
    }
    cached_key_ = key;
    cached_tile_ = found->second.get();
    return cached_tile_;
  }

  std::unordered_map<std::uint64_t, std::unique_ptr<Tile>> tiles_;
  std::uint64_t cached_key_{0};
  Tile* cached_tile_{nullptr};
};

}  // namespace patchy::ui
