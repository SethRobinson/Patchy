#pragma once

#include <cstdint>
#include <string>

namespace patchy {

enum class GpuPresentationApi : std::uint8_t {
  Unknown,
  OpenGL,
  Vulkan,
  Metal,
  Direct3D11,
  Direct3D12,
};

enum class ZeroCopyInteropStatus : std::uint8_t {
  Ready,
  UnknownApi,
  ApiMismatch,
  SharedDeviceRequired,
  NativeTextureImportRequired,
  SynchronizationRequired,
};

// These are observations made by the platform-specific presentation bridge.
// The evaluator deliberately does not inspect native handles: ownership,
// lifetime, and render-thread rules belong to that bridge. A native path is
// eligible only after every prerequisite has been positively established.
struct ZeroCopyInteropRequirements {
  GpuPresentationApi compositor_api{GpuPresentationApi::Unknown};
  GpuPresentationApi presentation_api{GpuPresentationApi::Unknown};
  bool shared_device{false};
  bool native_texture_import{false};
  bool synchronization{false};
};

struct ZeroCopyInteropDecision {
  ZeroCopyInteropStatus status{ZeroCopyInteropStatus::UnknownApi};
  std::string reason;

  [[nodiscard]] bool eligible() const noexcept {
    return status == ZeroCopyInteropStatus::Ready;
  }
};

// Pure capability gate for a future native presentation bridge. This function
// does not create, import, retain, or destroy a graphics resource.
[[nodiscard]] ZeroCopyInteropDecision evaluate_zero_copy_interop(
    const ZeroCopyInteropRequirements& requirements);

}  // namespace patchy
