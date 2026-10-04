#pragma once

#include "core/layer.hpp"
#include "render/dirty_region.hpp"
#include "render/gpu_render_graph.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace patchy {

struct GpuTileRenderPlan {
  Rect document_bounds{};
  std::int32_t tile_size{256};
  std::vector<TileKey> tiles;

  [[nodiscard]] bool empty() const noexcept { return tiles.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return tiles.size(); }
  [[nodiscard]] Rect tile_rect(TileKey key) const noexcept;
};

class GpuTileScheduler {
public:
  explicit GpuTileScheduler(std::int32_t tile_size = 256);

  [[nodiscard]] std::int32_t tile_size() const noexcept;
  [[nodiscard]] GpuTileRenderPlan full_plan(Rect document_bounds, std::int32_t mip = 0) const;
  [[nodiscard]] GpuTileRenderPlan dirty_plan(Rect document_bounds,
                                              const DirtyRegionSet& dirty) const;
  [[nodiscard]] RenderGraph build_graph(const GpuTileRenderPlan& plan) const;

private:
  std::int32_t tile_size_{256};
};

}  // namespace patchy
