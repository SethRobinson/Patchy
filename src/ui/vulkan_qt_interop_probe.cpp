#include "ui/vulkan_qt_interop_probe.hpp"

#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QtGlobal>

#include <cstdint>
#include <type_traits>

#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
#include <QVulkanFunctions>
#include <vulkan/vulkan.h>
#endif

namespace patchy::ui {
namespace {

#if QT_CONFIG(vulkan)
template <typename Handle>
std::uintptr_t handle_key(Handle handle) noexcept {
  if constexpr (std::is_pointer_v<Handle>) {
    return reinterpret_cast<std::uintptr_t>(handle);
  } else {
    return static_cast<std::uintptr_t>(handle);
  }
}
#endif

VulkanQtInteropReport unavailable_report(const QString& reason) {
  VulkanQtInteropReport report;
  report.summary = reason;
  report.decision = {patchy::ZeroCopyInteropStatus::UnknownApi, reason.toStdString()};
  return report;
}

}  // namespace

VulkanQtInteropReport probe_vulkan_qt_interop(
    QQuickWindow* window, const DawnVulkanInteropObservation& dawn) {
#if !QT_CONFIG(vulkan)
  Q_UNUSED(window);
  Q_UNUSED(dawn);
  return unavailable_report(QStringLiteral("Qt was built without Vulkan support"));
#else
  if (window == nullptr || window->rendererInterface() == nullptr) {
    return unavailable_report(QStringLiteral("Qt Quick did not expose a renderer interface"));
  }

  auto* renderer_interface = window->rendererInterface();
  if (renderer_interface->graphicsApi() != QSGRendererInterface::Vulkan) {
    const auto reason = QStringLiteral("Qt Quick is not presenting through Vulkan");
    auto report = unavailable_report(reason);
    report.decision = {patchy::ZeroCopyInteropStatus::ApiMismatch,
                       "zero-copy requires Qt Quick and Dawn to use Vulkan"};
    return report;
  }

  VulkanQtInteropReport report;
  report.qt_vulkan = true;
  const auto resource = [renderer_interface, window](QSGRendererInterface::Resource kind) {
    return renderer_interface->getResource(window, kind);
  };

  const auto* device = static_cast<const VkDevice*>(resource(QSGRendererInterface::DeviceResource));
  const auto* queue = static_cast<const VkQueue*>(resource(QSGRendererInterface::CommandQueueResource));
  const auto* physical_device =
      static_cast<const VkPhysicalDevice*>(resource(QSGRendererInterface::PhysicalDeviceResource));
  const auto* instance =
      static_cast<const QVulkanInstance*>(resource(QSGRendererInterface::VulkanInstanceResource));
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
  const auto* queue_family = static_cast<const std::uint32_t*>(
      resource(QSGRendererInterface::GraphicsQueueFamilyIndexResource));
  const auto* queue_index = static_cast<const std::uint32_t*>(
      resource(QSGRendererInterface::GraphicsQueueIndexResource));
#endif

  report.qt_device_observed = device != nullptr && *device != VK_NULL_HANDLE;
  report.qt_queue_observed = queue != nullptr && *queue != VK_NULL_HANDLE;
  report.qt_physical_device_observed = physical_device != nullptr && *physical_device != VK_NULL_HANDLE;
  report.qt_instance_observed = instance != nullptr && instance->vkInstance() != VK_NULL_HANDLE;
  if (report.qt_physical_device_observed && instance != nullptr && instance->functions() != nullptr) {
    VkPhysicalDeviceProperties properties{};
    instance->functions()->vkGetPhysicalDeviceProperties(*physical_device, &properties);
    report.qt_adapter_identity_observed = true;
    report.qt_vendor_id = properties.vendorID;
    report.qt_device_id = properties.deviceID;
  }
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
  report.qt_queue_family_observed = queue_family != nullptr;
  report.qt_queue_index_observed = queue_index != nullptr;
#endif
  report.dawn_instance_matches_qt = report.qt_instance_observed && dawn.native_instance_observed &&
                                    handle_key(instance->vkInstance()) == dawn.native_instance;
  report.dawn_adapter_identity_matches_qt = report.qt_adapter_identity_observed && dawn.adapter_identity_observed &&
                                            report.qt_vendor_id == dawn.vendor_id &&
                                            report.qt_device_id == dawn.device_id;

  patchy::ZeroCopyInteropRequirements requirements;
  requirements.compositor_api = dawn.compositor_api;
  requirements.presentation_api = patchy::GpuPresentationApi::Vulkan;
  // The current Dawn compositor owns a device created from its own adapter. It
  // does not adopt Qt's VkDevice, so observing both handles is not enough.
  requirements.shared_device = false;
  // Qt can wrap a VkImage, and Dawn can wrap external Vulkan memory, but this
  // path has not yet exported a compatible image plus allocation from Dawn.
  requirements.native_texture_import = false;
  // No external semaphore or queue-ownership protocol has been established.
  requirements.synchronization = false;
  report.decision = patchy::evaluate_zero_copy_interop(requirements);

  report.summary = QStringLiteral("Qt Vulkan resources: device=%1, queue=%2, physical-device=%3, instance=%4, queue-family=%5, queue-index=%6, vendor-id=%7, device-id=%8; Dawn instance=%9, adapter-observed=%10, instance-match=%11, adapter-match=%12, external-images=%13; native-device-adoption=no, qt-image-export=no, external-sync=no; zero-copy=%14")
                       .arg(report.qt_device_observed ? QStringLiteral("yes") : QStringLiteral("no"))
                       .arg(report.qt_queue_observed ? QStringLiteral("yes") : QStringLiteral("no"))
                       .arg(report.qt_physical_device_observed ? QStringLiteral("yes") : QStringLiteral("no"))
                       .arg(report.qt_instance_observed ? QStringLiteral("yes") : QStringLiteral("no"))
                       .arg(report.qt_queue_family_observed ? QStringLiteral("yes") : QStringLiteral("no"))
                       .arg(report.qt_queue_index_observed ? QStringLiteral("yes") : QStringLiteral("no"))
                       .arg(report.qt_adapter_identity_observed ? QString::number(report.qt_vendor_id, 16)
                                                               : QStringLiteral("unknown"))
                       .arg(report.qt_adapter_identity_observed ? QString::number(report.qt_device_id, 16)
                                                               : QStringLiteral("unknown"))
                       .arg(dawn.native_instance_observed ? QStringLiteral("yes") : QStringLiteral("no"))
                       .arg(dawn.adapter_identity_observed ? QStringLiteral("yes") : QStringLiteral("no"))
                       .arg(report.dawn_instance_matches_qt ? QStringLiteral("yes") : QStringLiteral("no"))
                       .arg(report.dawn_adapter_identity_matches_qt ? QStringLiteral("yes") : QStringLiteral("no"))
                       .arg(dawn.external_image_api_available ? QStringLiteral("yes") : QStringLiteral("no"))
                       .arg(QString::fromStdString(report.decision.reason));
  return report;
#endif
}

}  // namespace patchy::ui
