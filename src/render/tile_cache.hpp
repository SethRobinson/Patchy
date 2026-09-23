#pragma once

#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace patchy {

struct TileKey {
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t mip{0};

  [[nodiscard]] bool operator==(const TileKey& other) const noexcept;
};

struct TileKeyHash {
  [[nodiscard]] std::size_t operator()(const TileKey& key) const noexcept;
};

class TileCache {
public:
  explicit TileCache(std::int32_t tile_size = 256);

  [[nodiscard]] std::int32_t tile_size() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] std::optional<PixelBuffer> find(TileKey key) const;

  void put(TileKey key, PixelBuffer tile);
  void invalidate(TileKey key);
  // Invalidates cached tiles at `mip` whose tile rectangle intersects
  // `region` in document pixels. Returns the number of removed tiles.
  [[nodiscard]] std::size_t invalidate_region(Rect region, std::int32_t mip = 0);
  // Dirty document regions normally affect every derived mip.
  [[nodiscard]] std::size_t invalidate_all_mips(Rect region);
  void clear();

private:
  std::int32_t tile_size_{256};
  std::unordered_map<TileKey, PixelBuffer, TileKeyHash> tiles_;
};

}  // namespace patchy
