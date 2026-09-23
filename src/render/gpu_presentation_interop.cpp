#include "render/gpu_presentation_interop.hpp"

namespace patchy {

ZeroCopyInteropDecision evaluate_zero_copy_interop(
    const ZeroCopyInteropRequirements& requirements) {
  if (requirements.compositor_api == GpuPresentationApi::Unknown ||
      requirements.presentation_api == GpuPresentationApi::Unknown) {
    return {ZeroCopyInteropStatus::UnknownApi,
            "zero-copy requires a known compositor and presentation API"};
  }
  if (requirements.compositor_api != requirements.presentation_api) {
    return {ZeroCopyInteropStatus::ApiMismatch,
            "zero-copy requires the compositor and presentation path to use the same graphics API"};
  }
  if (!requirements.shared_device) {
    return {ZeroCopyInteropStatus::SharedDeviceRequired,
            "zero-copy requires a device shared by the compositor and presentation path"};
  }
  if (!requirements.native_texture_import) {
    return {ZeroCopyInteropStatus::NativeTextureImportRequired,
            "zero-copy requires a verified native texture import bridge"};
  }
  if (!requirements.synchronization) {
    return {ZeroCopyInteropStatus::SynchronizationRequired,
            "zero-copy requires explicit synchronization for the imported resource"};
  }
  return {ZeroCopyInteropStatus::Ready,
          "zero-copy prerequisites are satisfied"};
}

}  // namespace patchy
