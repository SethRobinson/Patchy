#pragma once

#include "render/gpu_presentation_interop.hpp"

#include <QString>

#include <cstdint>

class QQuickWindow;

namespace patchy::ui {

struct DawnVulkanInteropObservation {
  patchy::GpuPresentationApi compositor_api{patchy::GpuPresentationApi::Unknown};
  bool device_created{false};
  bool native_instance_observed{false};
  std::uintptr_t native_instance{0};
};

struct VulkanQtInteropReport {
  patchy::ZeroCopyInteropDecision decision;
  bool qt_vulkan{false};
  bool qt_device_observed{false};
  bool qt_queue_observed{false};
  bool qt_instance_observed{false};
  bool qt_physical_device_observed{false};
  bool qt_queue_family_observed{false};
  bool qt_queue_index_observed{false};
  bool dawn_instance_matches_qt{false};
  QString summary;
};

// Must be called on the Qt Quick scene-graph rendering thread. The report only
// observes handles and never imports, retains, destroys, or publishes a native
// texture. It is therefore safe to run as an opt-in diagnostic before a future
// platform-specific bridge is implemented.
[[nodiscard]] VulkanQtInteropReport probe_vulkan_qt_interop(
    QQuickWindow* window, const DawnVulkanInteropObservation& dawn);

}  // namespace patchy::ui
