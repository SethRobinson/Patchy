#pragma once

#include "render/gpu_render_graph.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace patchy {

enum class GpuBackendState : std::uint8_t {
  Uninitialized,
  Ready,
  Lost,
  Failed,
};

enum class GpuSubmitResult : std::uint8_t {
  Submitted,
  InvalidGraph,
  DeviceLost,
  BackendError,
};

struct GpuBackendInfo {
  std::string name;
  bool hardware_accelerated{false};
  bool supports_zero_copy{false};
};

// The application-specific Qt RHI and Dawn adapters will implement this
// contract in later tiers. Keeping it Qt-free lets state transitions and
// fallback policy be tested with a recording backend now.
class GpuRenderBackend {
public:
  virtual ~GpuRenderBackend() = default;

  virtual bool initialize() = 0;
  virtual GpuSubmitResult submit(const RenderGraph& graph) = 0;
  virtual bool recover() = 0;

  [[nodiscard]] virtual GpuBackendState state() const noexcept = 0;
  [[nodiscard]] virtual std::string_view last_error() const noexcept = 0;
  [[nodiscard]] virtual GpuBackendInfo info() const = 0;
};

}  // namespace patchy
