#pragma once

#include "render/compositor.hpp"
#include "render/gpu_document_capabilities.hpp"
#include "render/gpu_tile_scheduler.hpp"
#include "render/pixel_comparison.hpp"
#include "render/tile_cache.hpp"
#include "fake_gpu_backend.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace patchy::test {

struct FakeGpuRenderResult {
  bool success{false};
  bool recovered_device{false};
  std::size_t rendered_tiles{0};
  std::size_t cache_hits{0};
  PixelBuffer frame{};
  std::string error;
};

// This class deliberately uses the CPU compositor for each missing tile. It is
// a scheduling/equivalence oracle, not a claim of GPU execution. A native Qt
// RHI or Dawn backend can later consume the same plan and graph contract.
class FakeGpuDocumentRenderer final {
public:
  explicit FakeGpuDocumentRenderer(std::int32_t tile_size = 256)
      : scheduler_(tile_size), tile_cache_(tile_size) {}

  [[nodiscard]] FakeGpuRenderResult render(const Document& document,
                                           const DirtyRegionSet& dirty = {}) {
    FakeGpuRenderResult result;
    const auto capability = gpu_document_capability(document);
    if (!capability.supported()) {
      result.error = capability.reason;
      return result;
    }

    const Rect document_bounds{0, 0, document.width(), document.height()};
    if (document_bounds.empty()) {
      result.error = "document has no renderable canvas";
      return result;
    }
    if (frame_.width() != document.width() || frame_.height() != document.height() ||
        frame_.format() != PixelFormat::rgb8()) {
      frame_ = PixelBuffer{};
      tile_cache_.clear();
    }

    if (backend_.state() == GpuBackendState::Lost) {
      tile_cache_.clear();
      frame_ = PixelBuffer{};
      if (!backend_.recover()) {
        result.error = std::string(backend_.last_error());
        return result;
      }
      result.recovered_device = true;
    }
    if (backend_.state() == GpuBackendState::Uninitialized && !backend_.initialize()) {
      result.error = std::string(backend_.last_error());
      return result;
    }
    if (backend_.state() != GpuBackendState::Ready) {
      result.error = std::string(backend_.last_error());
      return result;
    }

    DirtyRegionSet effective_dirty = dirty;
    if (frame_.empty()) {
      effective_dirty.clear();
      effective_dirty.add(document_bounds);
    }
    for (const auto& region : effective_dirty.regions()) {
      if (region.mip != 0) {
        result.error = "fake renderer supports only mip 0 output";
        return result;
      }
      (void)tile_cache_.invalidate_region(region.bounds, region.mip);
    }

    const auto plan = scheduler_.dirty_plan(document_bounds, effective_dirty);
    if (plan.empty()) {
      result.success = true;
      result.frame = frame_;
      return result;
    }
    const auto graph = scheduler_.build_graph(plan);
    std::string graph_reason;
    if (!graph.validate(&graph_reason)) {
      result.error = graph_reason;
      return result;
    }
    auto submit_result = backend_.submit(graph);
    if (submit_result == GpuSubmitResult::DeviceLost) {
      tile_cache_.clear();
      frame_ = PixelBuffer{};
      if (!backend_.recover()) {
        result.error = std::string(backend_.last_error());
        return result;
      }
      result.recovered_device = true;
      effective_dirty.clear();
      effective_dirty.add(document_bounds);
      const auto retry_plan = scheduler_.dirty_plan(document_bounds, effective_dirty);
      const auto retry_graph = scheduler_.build_graph(retry_plan);
      submit_result = backend_.submit(retry_graph);
      if (submit_result != GpuSubmitResult::Submitted) {
        result.error = std::string(backend_.last_error());
        return result;
      }
      return render_tiles(document, retry_plan, result);
    }
    if (submit_result != GpuSubmitResult::Submitted) {
      result.error = std::string(backend_.last_error());
      return result;
    }
    return render_tiles(document, plan, result);
  }

  [[nodiscard]] PixelComparisonReport compare_with_cpu(const Document& document) const {
    if (!rendered_frame().empty()) {
      return compare_pixel_buffers(Compositor{}.flatten_rgb8(document), rendered_frame());
    }
    return {};
  }

  [[nodiscard]] const PixelBuffer& rendered_frame() const noexcept { return frame_; }
  [[nodiscard]] const TileCache& tile_cache() const noexcept { return tile_cache_; }
  [[nodiscard]] const FakeGpuBackend& backend() const noexcept { return backend_; }
  [[nodiscard]] FakeGpuBackend& backend() noexcept { return backend_; }

private:
  FakeGpuRenderResult render_tiles(const Document& document, const GpuTileRenderPlan& plan,
                                   FakeGpuRenderResult result) {
    if (frame_.empty()) {
      frame_ = PixelBuffer(document.width(), document.height(), PixelFormat::rgb8());
      frame_.clear(0);
    }
    const Compositor reference;
    for (const auto key : plan.tiles) {
      if (key.mip != 0) {
        result.error = "fake renderer supports only mip 0 output";
        return result;
      }
      const auto rect = plan.tile_rect(key);
      if (rect.empty()) {
        continue;
      }
      const auto cached = tile_cache_.find(key);
      PixelBuffer tile;
      if (cached.has_value()) {
        tile = *cached;
        ++result.cache_hits;
      } else {
        tile = reference.flatten_rgb8_region(document, rect);
        tile_cache_.put(key, tile);
        ++result.rendered_tiles;
      }
      for (std::int32_t y = 0; y < tile.height(); ++y) {
        for (std::int32_t x = 0; x < tile.width(); ++x) {
          const auto* source = tile.pixel(x, y);
          auto* destination = frame_.pixel(rect.x + x, rect.y + y);
          destination[0] = source[0];
          destination[1] = source[1];
          destination[2] = source[2];
        }
      }
    }
    result.success = true;
    result.frame = frame_;
    return result;
  }

  GpuTileScheduler scheduler_;
  TileCache tile_cache_;
  FakeGpuBackend backend_;
  PixelBuffer frame_{};
};

}  // namespace patchy::test
