#include "render/gpu_tile_scheduler.hpp"

#include "core/rect_utils.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace patchy {
namespace {

std::int32_t mip_scale(std::int32_t mip) noexcept {
  if (mip < 0 || mip > 30) {
    return 0;
  }
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(1) << mip);
}

std::int64_t scaled_tile_size(std::int32_t tile_size, std::int32_t mip) noexcept {
  const auto scale = mip_scale(mip);
  if (scale <= 0 || tile_size <= 0 ||
      static_cast<std::int64_t>(tile_size) >
          std::numeric_limits<std::int32_t>::max() / static_cast<std::int64_t>(scale)) {
    return 0;
  }
  return static_cast<std::int64_t>(tile_size) * scale;
}

std::int64_t floor_div(std::int64_t value, std::int64_t divisor) noexcept {
  const auto quotient = value / divisor;
  const auto remainder = value % divisor;
  return remainder != 0 && value < 0 ? quotient - 1 : quotient;
}

std::int64_t ceil_div(std::int64_t value, std::int64_t divisor) noexcept {
  const auto quotient = value / divisor;
  const auto remainder = value % divisor;
  return remainder != 0 && value > 0 ? quotient + 1 : quotient;
}

void add_region_tiles(std::vector<TileKey>& keys, Rect document_bounds, Rect region,
                      std::int32_t tile_size, std::int32_t mip) {
  const auto scaled_tile = scaled_tile_size(tile_size, mip);
  if (scaled_tile <= 0) {
    return;
  }
  const auto clipped = intersect_rect(document_bounds, region);
  if (clipped.empty()) {
    return;
  }
  const auto first_x = floor_div(static_cast<std::int64_t>(clipped.x) - document_bounds.x, scaled_tile);
  const auto first_y = floor_div(static_cast<std::int64_t>(clipped.y) - document_bounds.y, scaled_tile);
  const auto last_x = ceil_div(static_cast<std::int64_t>(clipped.x) + clipped.width - document_bounds.x,
                               scaled_tile);
  const auto last_y = ceil_div(static_cast<std::int64_t>(clipped.y) + clipped.height - document_bounds.y,
                               scaled_tile);
  for (auto y = first_y; y < last_y; ++y) {
    for (auto x = first_x; x < last_x; ++x) {
      if (x >= std::numeric_limits<std::int32_t>::min() &&
          x <= std::numeric_limits<std::int32_t>::max() &&
          y >= std::numeric_limits<std::int32_t>::min() &&
          y <= std::numeric_limits<std::int32_t>::max()) {
        keys.push_back(TileKey{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), mip});
      }
    }
  }
}

void sort_and_unique(std::vector<TileKey>& keys) {
  std::sort(keys.begin(), keys.end(), [](const TileKey& left, const TileKey& right) {
    if (left.mip != right.mip) {
      return left.mip < right.mip;
    }
    if (left.y != right.y) {
      return left.y < right.y;
    }
    return left.x < right.x;
  });
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
}

std::string tile_name(std::string_view prefix, TileKey key) {
  return std::string(prefix) + "_" + std::to_string(key.mip) + "_" +
         std::to_string(key.x) + "_" + std::to_string(key.y);
}

}  // namespace

GpuTileScheduler::GpuTileScheduler(std::int32_t tile_size) : tile_size_(tile_size) {
  if (tile_size <= 0) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Tile size must be positive"));
  }
}

std::int32_t GpuTileScheduler::tile_size() const noexcept {
  return tile_size_;
}

GpuTileRenderPlan GpuTileScheduler::full_plan(Rect document_bounds, std::int32_t mip) const {
  GpuTileRenderPlan plan;
  plan.document_bounds = document_bounds;
  plan.tile_size = tile_size_;
  add_region_tiles(plan.tiles, document_bounds, document_bounds, tile_size_, mip);
  sort_and_unique(plan.tiles);
  return plan;
}

GpuTileRenderPlan GpuTileScheduler::dirty_plan(Rect document_bounds,
                                               const DirtyRegionSet& dirty) const {
  GpuTileRenderPlan plan;
  plan.document_bounds = document_bounds;
  plan.tile_size = tile_size_;
  for (const auto& region : dirty.regions()) {
    add_region_tiles(plan.tiles, document_bounds, region.bounds, tile_size_, region.mip);
  }
  sort_and_unique(plan.tiles);
  return plan;
}

Rect GpuTileRenderPlan::tile_rect(TileKey key) const noexcept {
  const auto scaled_tile = scaled_tile_size(tile_size, key.mip);
  if (scaled_tile <= 0) {
    return {};
  }
  const auto left = static_cast<std::int64_t>(document_bounds.x) +
                    static_cast<std::int64_t>(key.x) * scaled_tile;
  const auto top = static_cast<std::int64_t>(document_bounds.y) +
                   static_cast<std::int64_t>(key.y) * scaled_tile;
  const auto right = left + scaled_tile;
  const auto bottom = top + scaled_tile;
  if (left < std::numeric_limits<std::int32_t>::min() ||
      top < std::numeric_limits<std::int32_t>::min() ||
      right > std::numeric_limits<std::int32_t>::max() + static_cast<std::int64_t>(1) ||
      bottom > std::numeric_limits<std::int32_t>::max() + static_cast<std::int64_t>(1)) {
    return {};
  }
  const auto raw = Rect{static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
                        static_cast<std::int32_t>(scaled_tile), static_cast<std::int32_t>(scaled_tile)};
  return intersect_rect(raw, document_bounds);
}

RenderGraph GpuTileScheduler::build_graph(const GpuTileRenderPlan& plan) const {
  RenderGraph graph;
  for (const auto tile : plan.tiles) {
    const auto bounds = plan.tile_rect(tile);
    if (bounds.empty()) {
      continue;
    }
    const auto source = graph.add_resource(tile_name("source", tile), bounds,
                                           RenderPixelFormat::Rgba8Unorm, true);
    const auto backdrop = graph.add_resource(tile_name("backdrop", tile), bounds,
                                             RenderPixelFormat::Rgba8Unorm);
    const auto output = graph.add_resource(tile_name("output", tile), bounds,
                                           RenderPixelFormat::Rgba8Unorm, true);
    const auto clear = graph.add_pass(tile_name("clear", tile), RenderPassType::Clear, tile);
    const auto composite = graph.add_pass(tile_name("composite", tile), RenderPassType::Composite, tile);
    const auto readback = graph.add_pass(tile_name("readback", tile), RenderPassType::Readback, tile);
    (void)graph.add_write(clear, backdrop);
    (void)graph.add_read(composite, source);
    (void)graph.add_read(composite, backdrop);
    (void)graph.add_write(composite, output);
    (void)graph.add_read(readback, output);
  }
  return graph;
}

}  // namespace patchy
