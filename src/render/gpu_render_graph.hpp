#pragma once

#include "core/layer.hpp"
#include "render/tile_cache.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace patchy {

enum class RenderPixelFormat : std::uint8_t {
  Rgba8Unorm,
  Rgba16Float,
  Rgba32Float,
};

enum class RenderPassType : std::uint8_t {
  Clear,
  Composite,
  Filter,
  Copy,
  Readback,
};

using RenderResourceId = std::uint32_t;
using RenderPassId = std::uint32_t;

struct RenderResourceDescriptor {
  std::string name;
  Rect bounds;
  RenderPixelFormat format{RenderPixelFormat::Rgba8Unorm};
  bool external{false};
};

struct RenderPassDescriptor {
  RenderPassId id{0};
  std::string name;
  RenderPassType type{RenderPassType::Composite};
  TileKey tile;
  std::vector<RenderResourceId> reads;
  std::vector<RenderResourceId> writes;
};

// A render graph uses explicit resources and pass dependencies. It is kept
// independent of Qt, Dawn, and any native graphics API so validation and fake
// backend tests run on CPU-only machines.
class RenderGraph {
public:
  [[nodiscard]] RenderResourceId add_resource(std::string name, Rect bounds, RenderPixelFormat format,
                                               bool external = false);
  [[nodiscard]] RenderPassId add_pass(std::string name, RenderPassType type, TileKey tile = {});

  [[nodiscard]] bool add_read(RenderPassId pass, RenderResourceId resource);
  [[nodiscard]] bool add_write(RenderPassId pass, RenderResourceId resource);

  [[nodiscard]] const RenderResourceDescriptor* resource(RenderResourceId id) const noexcept;
  [[nodiscard]] const RenderPassDescriptor* pass(RenderPassId id) const noexcept;
  [[nodiscard]] const std::vector<RenderResourceDescriptor>& resources() const noexcept;
  [[nodiscard]] const std::vector<RenderPassDescriptor>& passes() const noexcept;

  // Reports the first structural error and never contacts a driver.
  [[nodiscard]] bool validate(std::string* reason = nullptr) const;

  // Returns a stable topological order. Independent passes retain insertion
  // order so command recording is deterministic across toolchains.
  [[nodiscard]] std::vector<RenderPassId> execution_order(std::string* reason = nullptr) const;

  void clear() noexcept;

private:
  std::vector<RenderResourceDescriptor> resources_;
  std::vector<RenderPassDescriptor> passes_;
};

}  // namespace patchy
