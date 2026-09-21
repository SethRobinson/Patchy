#include "ui/canvas_graphics_surface.hpp"

#ifndef PATCHY_GPU_CANVAS

namespace patchy::ui {

std::unique_ptr<CanvasGraphicsSurface> CanvasGraphicsSurface::create(QWidget* parent) {
  Q_UNUSED(parent);
  return nullptr;
}

CanvasGraphicsSurface::~CanvasGraphicsSurface() = default;

CanvasGraphicsApi CanvasGraphicsSurface::api() const noexcept {
  return CanvasGraphicsApi::Unknown;
}

void CanvasGraphicsSurface::set_frame(QImage frame) {
  Q_UNUSED(frame);
}

void CanvasGraphicsSurface::request_update(const QRegion& region) {
  Q_UNUSED(region);
}

void CanvasGraphicsSurface::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
}

}  // namespace patchy::ui

#else

#include "core/environment.hpp"

#include <QImage>
#include <QGuiApplication>
#include <QMetaObject>
#include <QPainter>
#include <QQuickPaintedItem>
#include <QQuickWindow>
#include <QQuickWidget>
#include <QSGRendererInterface>
#include <QTimer>
#include <QUrl>
#include <QtGlobal>

#if QT_CONFIG(opengl)
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#endif

#if QT_CONFIG(vulkan)
#include <QVulkanFunctions>
#include <QVulkanInstance>
#include <vulkan/vulkan.h>
#endif

#ifdef Q_OS_WIN
#include <d3d11.h>
#include <dxgi1_6.h>
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
#include <d3d12.h>
#endif
#endif

#include <algorithm>
#include <cctype>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace patchy::ui {

namespace {

enum class GraphicsPreference : std::uint8_t {
  Auto,
  Cpu,
  OpenGL,
  Vulkan,
  Metal,
  Direct3D11,
  Direct3D12
};

struct SceneGraphProbe {
  CanvasGraphicsApi api{CanvasGraphicsApi::Unknown};
  QString adapter_name;
  bool hardware_accelerated{false};
  QString failure_reason;
};

QString normalized_preference(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return QString::fromStdString(std::move(value));
}

GraphicsPreference graphics_preference() {
  std::optional<std::string> requested = patchy::environment_variable("PATCHY_RENDER_BACKEND");
  if (!requested.has_value()) {
    requested = patchy::environment_variable("PATCHY_GPU_CANVAS");
  }
  if (!requested.has_value() || requested->empty()) {
    return GraphicsPreference::Auto;
  }

  const auto value = normalized_preference(*requested);
  if (value == QStringLiteral("cpu") || value == QStringLiteral("software")) {
    return GraphicsPreference::Cpu;
  }
  if (value == QStringLiteral("auto") || value == QStringLiteral("gpu")) {
    return GraphicsPreference::Auto;
  }
  if (value == QStringLiteral("opengl") || value == QStringLiteral("gl")) {
    return GraphicsPreference::OpenGL;
  }
  if (value == QStringLiteral("vulkan") || value == QStringLiteral("vk")) {
    return GraphicsPreference::Vulkan;
  }
  if (value == QStringLiteral("metal")) {
    return GraphicsPreference::Metal;
  }
  if (value == QStringLiteral("d3d11") || value == QStringLiteral("direct3d11") ||
      value == QStringLiteral("directx11")) {
    return GraphicsPreference::Direct3D11;
  }
  if (value == QStringLiteral("d3d12") || value == QStringLiteral("direct3d12") ||
      value == QStringLiteral("directx12")) {
    return GraphicsPreference::Direct3D12;
  }

  qInfo().noquote() << "Unknown PATCHY_RENDER_BACKEND value:" << value << "; using auto";
  return GraphicsPreference::Auto;
}

QSGRendererInterface::GraphicsApi qt_api_for_preference(GraphicsPreference preference) {
  switch (preference) {
  case GraphicsPreference::OpenGL:
    return QSGRendererInterface::OpenGL;
  case GraphicsPreference::Vulkan:
    return QSGRendererInterface::Vulkan;
  case GraphicsPreference::Metal:
    return QSGRendererInterface::Metal;
  case GraphicsPreference::Direct3D11:
    return QSGRendererInterface::Direct3D11;
  case GraphicsPreference::Direct3D12:
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    return QSGRendererInterface::Direct3D12;
#else
    return QSGRendererInterface::Unknown;
#endif
  case GraphicsPreference::Auto:
  case GraphicsPreference::Cpu:
    return QSGRendererInterface::Unknown;
  }
  return QSGRendererInterface::Unknown;
}

void configure_qt_quick_api(GraphicsPreference preference) {
  static std::once_flag configured;
  std::call_once(configured, [preference] {
    const auto api = qt_api_for_preference(preference);
    if (api != QSGRendererInterface::Unknown) {
      QQuickWindow::setGraphicsApi(api);
    }
  });
}

bool known_software_renderer(QString name) {
  name = name.toLower();
  constexpr const char* kSoftwareMarkers[] = {
      "llvmpipe", "softpipe", "swrast", "software rasterizer", "swiftshader",
      "microsoft basic render driver", "warp", "lavapipe", "softgpu"};
  for (const auto* marker : kSoftwareMarkers) {
    if (name.contains(QString::fromLatin1(marker))) {
      return true;
    }
  }
  return false;
}

CanvasGraphicsApi canvas_api_for_qt_api(QSGRendererInterface::GraphicsApi api) {
  switch (api) {
  case QSGRendererInterface::OpenGL:
    return CanvasGraphicsApi::OpenGL;
  case QSGRendererInterface::Vulkan:
    return CanvasGraphicsApi::Vulkan;
  case QSGRendererInterface::Metal:
    return CanvasGraphicsApi::Metal;
  case QSGRendererInterface::Direct3D11:
    return CanvasGraphicsApi::Direct3D11;
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
  case QSGRendererInterface::Direct3D12:
    return CanvasGraphicsApi::Direct3D12;
#endif
  case QSGRendererInterface::Software:
  case QSGRendererInterface::Unknown:
  case QSGRendererInterface::OpenVG:
  case QSGRendererInterface::Null:
    return CanvasGraphicsApi::Unknown;
  }
  return CanvasGraphicsApi::Unknown;
}

QString canvas_graphics_api_name(CanvasGraphicsApi api) {
  switch (api) {
  case CanvasGraphicsApi::OpenGL:
    return QStringLiteral("OpenGL");
  case CanvasGraphicsApi::Vulkan:
    return QStringLiteral("Vulkan");
  case CanvasGraphicsApi::Metal:
    return QStringLiteral("Metal");
  case CanvasGraphicsApi::Direct3D11:
    return QStringLiteral("Direct3D11");
  case CanvasGraphicsApi::Direct3D12:
    return QStringLiteral("Direct3D12");
  case CanvasGraphicsApi::Unknown:
    return QStringLiteral("Unknown");
  }
  return QStringLiteral("Unknown");
}

#if QT_CONFIG(opengl)
QString probe_opengl_adapter(QQuickWindow* window, bool& hardware) {
  auto* renderer_interface = window->rendererInterface();
  auto* resource = renderer_interface->getResource(window, QSGRendererInterface::OpenGLContextResource);
  auto* context = static_cast<QOpenGLContext*>(resource);
  if (context == nullptr || context->functions() == nullptr) {
    hardware = false;
    return QStringLiteral("OpenGL adapter unavailable");
  }
  const auto* renderer = context->functions()->glGetString(GL_RENDERER);
  const auto* vendor = context->functions()->glGetString(GL_VENDOR);
  const QString renderer_name = renderer != nullptr ? QString::fromUtf8(reinterpret_cast<const char*>(renderer))
                                                     : QStringLiteral("Unknown OpenGL renderer");
  const QString vendor_name = vendor != nullptr ? QString::fromUtf8(reinterpret_cast<const char*>(vendor))
                                                 : QStringLiteral("Unknown vendor");
  const auto adapter = vendor_name + QStringLiteral(" / ") + renderer_name;
  hardware = !known_software_renderer(adapter);
  return adapter;
}
#endif

#if QT_CONFIG(vulkan)
QString probe_vulkan_adapter(QQuickWindow* window, bool& hardware) {
  auto* renderer_interface = window->rendererInterface();
  auto* instance_resource = renderer_interface->getResource(window, QSGRendererInterface::VulkanInstanceResource);
  auto* physical_resource = renderer_interface->getResource(window, QSGRendererInterface::PhysicalDeviceResource);
  auto* instance = static_cast<QVulkanInstance*>(instance_resource);
  auto* physical_device = static_cast<VkPhysicalDevice*>(physical_resource);
  if (instance == nullptr || physical_device == nullptr || instance->functions() == nullptr) {
    hardware = false;
    return QStringLiteral("Vulkan adapter unavailable");
  }

  VkPhysicalDeviceProperties properties{};
  instance->functions()->vkGetPhysicalDeviceProperties(*physical_device, &properties);
  const QString adapter = QString::fromUtf8(properties.deviceName);
  hardware = properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU && !known_software_renderer(adapter);
  return adapter.isEmpty() ? QStringLiteral("Unknown Vulkan adapter") : adapter;
}
#endif

#ifdef Q_OS_WIN
template <typename Device>
QString probe_d3d_adapter(Device* device, bool& hardware) {
  if (device == nullptr) {
    hardware = false;
    return QStringLiteral("Direct3D adapter unavailable");
  }

  IDXGIDevice* dxgi_device = nullptr;
  if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device))) ||
      dxgi_device == nullptr) {
    hardware = false;
    return QStringLiteral("Direct3D adapter unavailable");
  }

  IDXGIAdapter* adapter = nullptr;
  const HRESULT adapter_result = dxgi_device->GetAdapter(&adapter);
  dxgi_device->Release();
  if (FAILED(adapter_result) || adapter == nullptr) {
    hardware = false;
    return QStringLiteral("Direct3D adapter unavailable");
  }

  IDXGIAdapter1* adapter1 = nullptr;
  const HRESULT adapter1_result = adapter->QueryInterface(__uuidof(IDXGIAdapter1),
                                                           reinterpret_cast<void**>(&adapter1));
  adapter->Release();
  if (FAILED(adapter1_result) || adapter1 == nullptr) {
    hardware = false;
    return QStringLiteral("Direct3D adapter unavailable");
  }

  DXGI_ADAPTER_DESC1 description{};
  const HRESULT description_result = adapter1->GetDesc1(&description);
  adapter1->Release();
  if (FAILED(description_result)) {
    hardware = false;
    return QStringLiteral("Direct3D adapter unavailable");
  }

  const QString name = QString::fromWCharArray(description.Description);
  hardware = (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 && !known_software_renderer(name);
  return name;
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
QString probe_d3d12_adapter(ID3D12Device* device, bool& hardware) {
  if (device == nullptr) {
    hardware = false;
    return QStringLiteral("Direct3D 12 adapter unavailable");
  }

  IDXGIFactory4* factory = nullptr;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory))) ||
      factory == nullptr) {
    hardware = false;
    return QStringLiteral("Direct3D 12 adapter unavailable");
  }

  IDXGIAdapter1* adapter = nullptr;
  const HRESULT adapter_result = factory->EnumAdapterByLuid(
      device->GetAdapterLuid(), __uuidof(IDXGIAdapter1), reinterpret_cast<void**>(&adapter));
  factory->Release();
  if (FAILED(adapter_result) || adapter == nullptr) {
    hardware = false;
    return QStringLiteral("Direct3D 12 adapter unavailable");
  }

  DXGI_ADAPTER_DESC1 description{};
  const HRESULT description_result = adapter->GetDesc1(&description);
  adapter->Release();
  if (FAILED(description_result)) {
    hardware = false;
    return QStringLiteral("Direct3D 12 adapter unavailable");
  }

  const QString name = QString::fromWCharArray(description.Description);
  hardware = (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 && !known_software_renderer(name);
  return name;
}
#endif
#endif

SceneGraphProbe probe_scene_graph(QQuickWindow* window) {
  SceneGraphProbe result;
  if (window == nullptr || window->rendererInterface() == nullptr) {
    result.failure_reason = QStringLiteral("Qt Quick did not expose a renderer interface");
    return result;
  }

  auto* renderer_interface = window->rendererInterface();
  const auto qt_api = renderer_interface->graphicsApi();
  result.api = canvas_api_for_qt_api(qt_api);
  if (qt_api == QSGRendererInterface::Software) {
    result.failure_reason = QStringLiteral("Qt Quick selected its software scene graph");
    return result;
  }
  if (result.api == CanvasGraphicsApi::Unknown) {
    result.failure_reason = QStringLiteral("Qt Quick selected an unsupported graphics API");
    return result;
  }

  result.hardware_accelerated = true;
  switch (qt_api) {
#if QT_CONFIG(opengl)
  case QSGRendererInterface::OpenGL:
    result.adapter_name = probe_opengl_adapter(window, result.hardware_accelerated);
    break;
#endif
#if QT_CONFIG(vulkan)
  case QSGRendererInterface::Vulkan:
    result.adapter_name = probe_vulkan_adapter(window, result.hardware_accelerated);
    break;
#endif
#ifdef Q_OS_WIN
  case QSGRendererInterface::Direct3D11: {
    auto* resource = renderer_interface->getResource(window, QSGRendererInterface::DeviceResource);
    result.adapter_name = probe_d3d_adapter(static_cast<ID3D11Device*>(resource), result.hardware_accelerated);
    break;
  }
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
  case QSGRendererInterface::Direct3D12: {
    auto* resource = renderer_interface->getResource(window, QSGRendererInterface::DeviceResource);
    result.adapter_name = probe_d3d12_adapter(static_cast<ID3D12Device*>(resource), result.hardware_accelerated);
    break;
  }
#endif
#endif
  case QSGRendererInterface::Metal:
    result.adapter_name = QStringLiteral("Metal device");
    break;
  default:
    break;
  }

  if (!result.hardware_accelerated) {
    result.failure_reason = QStringLiteral("Qt Quick selected a software graphics adapter: ") + result.adapter_name;
  }
  return result;
}

}  // namespace

class CanvasGraphicsSurface::QuickCanvasItem final : public QQuickPaintedItem {
public:
  explicit QuickCanvasItem() {
    setFillColor(Qt::transparent);
    setRenderTarget(QQuickPaintedItem::Image);
    setAntialiasing(false);
  }

  void set_frame(QImage frame) {
    {
      const std::lock_guard lock(frame_mutex_);
      frame_ = std::move(frame);
    }
    update();
  }

  void paint(QPainter* painter) override {
    QImage frame;
    {
      const std::lock_guard lock(frame_mutex_);
      frame = frame_;
    }
    painter->fillRect(boundingRect(), Qt::transparent);
    if (!frame.isNull()) {
      painter->drawImage(QPointF(0.0, 0.0), frame);
    }
  }

private:
  std::mutex frame_mutex_;
  QImage frame_;
};

std::unique_ptr<CanvasGraphicsSurface> CanvasGraphicsSurface::create(QWidget* parent) {
  const auto platform = QGuiApplication::platformName();
  if (platform == QStringLiteral("offscreen") || platform == QStringLiteral("minimal") ||
      platform == QStringLiteral("minimalegl")) {
    return nullptr;
  }
  const auto preference = graphics_preference();
  if (preference == GraphicsPreference::Cpu) {
    return nullptr;
  }
  if (preference != GraphicsPreference::Auto && qt_api_for_preference(preference) == QSGRendererInterface::Unknown) {
    qInfo() << "Requested graphics backend is not available in this Qt build; using CPU canvas";
    return nullptr;
  }
  configure_qt_quick_api(preference);
  return std::unique_ptr<CanvasGraphicsSurface>(new CanvasGraphicsSurface(parent));
}

CanvasGraphicsSurface::CanvasGraphicsSurface(QWidget* parent) : QWidget(parent) {
  setAutoFillBackground(false);
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setFocusPolicy(Qt::NoFocus);

  quick_widget_ = new QQuickWidget(this);
  quick_widget_->setObjectName(QStringLiteral("canvasGraphicsQuickWidget"));
  quick_widget_->setResizeMode(QQuickWidget::SizeRootObjectToView);
  quick_widget_->setClearColor(Qt::transparent);
  quick_widget_->setAttribute(Qt::WA_TransparentForMouseEvents);
  quick_widget_->setFocusPolicy(Qt::NoFocus);

  quick_item_ = new QuickCanvasItem;
  quick_item_->setSize(size());
  quick_widget_->setContent(QUrl(), nullptr, quick_item_);

  connect(quick_widget_, &QQuickWidget::sceneGraphError, this,
          [this](QQuickWindow::SceneGraphError error, const QString& message) {
            scene_graph_error(static_cast<int>(error), message);
          });
  connect(quick_widget_->quickWindow(), &QQuickWindow::beforeRendering, this,
          [this] { set_api_from_scene_graph(); }, Qt::DirectConnection);
  connect(quick_widget_->quickWindow(), &QQuickWindow::sceneGraphInvalidated, this, [this] {
    QMetaObject::invokeMethod(this, [this] {
      if (api_ != CanvasGraphicsApi::Unknown) {
        emit failed(QStringLiteral("Qt Quick graphics device was lost"));
      }
    }, Qt::QueuedConnection);
  }, Qt::DirectConnection);
  quick_widget_->setGeometry(rect());
  QTimer::singleShot(2000, this, [this] {
    if (api_ == CanvasGraphicsApi::Unknown && quick_widget_ != nullptr) {
      const auto reason = quick_widget_->status() == QQuickWidget::Error
                              ? QStringLiteral("Qt Quick reported a scene graph error")
                              : QStringLiteral("Qt Quick scene graph initialization timed out");
      emit failed(reason);
    }
  });
}

CanvasGraphicsSurface::~CanvasGraphicsSurface() = default;

CanvasGraphicsApi CanvasGraphicsSurface::api() const noexcept {
  return api_;
}

void CanvasGraphicsSurface::set_api_from_scene_graph() {
  if (scene_graph_probe_started_) {
    return;
  }
  scene_graph_probe_started_ = true;
  const auto probe = probe_scene_graph(quick_widget_->quickWindow());
  QMetaObject::invokeMethod(this, [this, probe] {
    if (!probe.failure_reason.isEmpty()) {
      emit failed(probe.failure_reason);
      return;
    }
    api_ = probe.api;
    qInfo().noquote() << "Patchy graphics backend:" << canvas_graphics_api_name(api_)
                      << ", adapter:" << probe.adapter_name
                      << ", hardware acceleration:" << (probe.hardware_accelerated ? "yes" : "no");
    emit ready(api_);
  }, Qt::QueuedConnection);
}

void CanvasGraphicsSurface::scene_graph_error(int error, const QString& message) {
  Q_UNUSED(error);
  const auto reason = message.isEmpty() ? QStringLiteral("Qt Quick could not initialize its graphics scene")
                                        : message;
  QMetaObject::invokeMethod(this, [this, reason] { emit failed(reason); }, Qt::QueuedConnection);
}

void CanvasGraphicsSurface::set_frame(QImage frame) {
  if (quick_item_ != nullptr) {
    quick_item_->set_frame(std::move(frame));
  }
}

void CanvasGraphicsSurface::request_update(const QRegion& region) {
  Q_UNUSED(region);
  if (quick_item_ != nullptr) {
    quick_item_->update();
  }
}

void CanvasGraphicsSurface::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (quick_widget_ != nullptr) {
    quick_widget_->setGeometry(rect());
  }
  if (quick_item_ != nullptr) {
    quick_item_->setSize(size());
  }
}

}  // namespace patchy::ui

#endif
