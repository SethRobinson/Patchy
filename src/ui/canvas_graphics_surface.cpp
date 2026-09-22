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

bool CanvasGraphicsSurface::set_gpu_document(CanvasGpuDocument document) {
  Q_UNUSED(document);
  return false;
}

void CanvasGraphicsSurface::clear_gpu_document() {}

void CanvasGraphicsSurface::request_update(const QRegion& region) {
  Q_UNUSED(region);
}

void CanvasGraphicsSurface::set_overlay_painter(std::function<void(QPainter&, QRect)> painter) {
  Q_UNUSED(painter);
}

void CanvasGraphicsSurface::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
}

}  // namespace patchy::ui

#else

#include "core/environment.hpp"
#ifdef PATCHY_GPU_SHADER_COMPOSITOR
#include "ui/gpu_shader_compositor.hpp"
#endif

#include <QImage>
#include <QGuiApplication>
#include <QMetaObject>
#include <QPainter>
#include <QPaintEvent>
#include <QQuickPaintedItem>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickWidget>
#include <QSGRendererInterface>
#include <QSGSimpleTextureNode>
#include <QSGNode>
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
#include <cmath>
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
  if (value == QStringLiteral("auto") || value == QStringLiteral("gpu") || value == QStringLiteral("webgpu")) {
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

class CanvasGraphicsSurface::QuickCanvasItem final : public QQuickItem {
public:
  class Background final : public QQuickPaintedItem {
  public:
    explicit Background(QQuickItem* parent) : QQuickPaintedItem(parent) {
      setFillColor(Qt::transparent);
      setRenderTarget(QQuickPaintedItem::Image);
      setAntialiasing(false);
    }

    void set_document_rect(QRectF rect, QColor backdrop) {
      rect_ = rect;
      backdrop_ = std::move(backdrop);
      update();
    }

    void paint(QPainter* painter) override {
      painter->fillRect(boundingRect(), backdrop_);
      if (rect_.isEmpty()) {
        return;
      }
      painter->save();
      painter->setClipRect(rect_);
      constexpr qreal square = 12.0;
      const auto left = std::floor(rect_.left() / square) * square;
      const auto top = std::floor(rect_.top() / square) * square;
      for (qreal y = top; y < rect_.bottom(); y += square) {
        for (qreal x = left; x < rect_.right(); x += square) {
          const auto column = static_cast<int>(std::floor((x - left) / square));
          const auto row = static_cast<int>(std::floor((y - top) / square));
          painter->fillRect(QRectF(x, y, square, square),
                            ((column + row) & 1) == 0 ? QColor(188, 188, 188) : QColor(236, 236, 236));
        }
      }
      painter->restore();
    }

  private:
    QRectF rect_;
    QColor backdrop_{Qt::transparent};
  };

  class Layers final : public QQuickItem {
  public:
    class LayerNode final : public QSGOpacityNode {
    public:
      std::uint64_t id{0};
      std::uint64_t revision{0};
      QSGSimpleTextureNode* texture_node{nullptr};

      LayerNode() {
        texture_node = new QSGSimpleTextureNode;
        texture_node->setFlag(QSGNode::OwnedByParent);
        appendChildNode(texture_node);
        setFlag(QSGNode::OwnedByParent);
      }
    };

    explicit Layers(QQuickItem* parent) : QQuickItem(parent) {
      setFlag(QQuickItem::ItemHasContents, true);
    }

    void set_document(CanvasGpuDocument document) {
      {
        const std::lock_guard lock(document_mutex_);
        document_ = std::move(document);
      }
      update();
    }

    QSGNode* updatePaintNode(QSGNode* old_node, UpdatePaintNodeData*) override {
      auto* root = old_node != nullptr ? old_node : new QSGNode;
      CanvasGpuDocument document;
      {
        const std::lock_guard lock(document_mutex_);
        document = document_;
      }
      auto* window = this->window();
      if (window == nullptr) {
        return root;
      }
      std::size_t index = 0;
      for (auto& layer : document.layers) {
        if (layer.image.isNull() || layer.rect.isEmpty() || layer.opacity <= 0.0) {
          continue;
        }
        auto* node = index < static_cast<std::size_t>(root->childCount())
                         ? dynamic_cast<LayerNode*>(root->childAtIndex(static_cast<int>(index)))
                         : nullptr;
        if (node == nullptr || node->id != layer.id) {
          auto* replacement = new LayerNode;
          if (index < static_cast<std::size_t>(root->childCount())) {
            auto* before = root->childAtIndex(static_cast<int>(index));
            root->insertChildNodeBefore(replacement, before);
            root->removeChildNode(before);
            delete before;
          } else {
            root->appendChildNode(replacement);
          }
          node = replacement;
        }
        node->setOpacity(static_cast<qreal>(layer.opacity));
        node->texture_node->setRect(layer.rect);
        node->texture_node->setFiltering(document.smooth_scaling ? QSGTexture::Linear : QSGTexture::Nearest);
        if (node->revision != layer.revision || node->texture_node->texture() == nullptr) {
          auto image = layer.image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
          node->texture_node->setOwnsTexture(false);
          delete node->texture_node->texture();
          node->texture_node->setTexture(window->createTextureFromImage(image, QQuickWindow::TextureHasAlphaChannel));
          if (node->texture_node->texture() == nullptr) {
            node->texture_node->setOwnsTexture(false);
            ++index;
            continue;
          }
          node->texture_node->setOwnsTexture(true);
          node->revision = layer.revision;
        }
        ++index;
      }
      while (root->childCount() > static_cast<int>(index)) {
        auto* stale = root->lastChild();
        root->removeChildNode(stale);
        delete stale;
      }
      return root;
    }

  private:
    std::mutex document_mutex_;
    CanvasGpuDocument document_;
  };

  class CompositeFrame final : public QQuickItem {
  public:
    explicit CompositeFrame(QQuickItem* parent) : QQuickItem(parent) {
      setFlag(QQuickItem::ItemHasContents, true);
    }

    void set_frame(QImage image, QRectF rect, bool smooth) {
      image_ = std::move(image);
      rect_ = rect;
      smooth_ = smooth;
      ++revision_;
      update();
    }

    void clear_frame() {
      image_ = {};
      rect_ = {};
      ++revision_;
      update();
    }

    QSGNode* updatePaintNode(QSGNode* old_node, UpdatePaintNodeData*) override {
      auto* node = static_cast<QSGSimpleTextureNode*>(old_node);
      if (node == nullptr) {
        node = new QSGSimpleTextureNode;
      }
      auto* window = this->window();
      if (window == nullptr || image_.isNull() || rect_.isEmpty()) {
        node->setRect(QRectF());
        return node;
      }
      if (texture_revision_ != revision_ || node->texture() == nullptr) {
        node->setOwnsTexture(false);
        delete node->texture();
        const auto image = image_.format() == QImage::Format_RGBA8888_Premultiplied
                               ? image_
                               : image_.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        node->setTexture(window->createTextureFromImage(image, QQuickWindow::TextureHasAlphaChannel));
        node->setOwnsTexture(true);
        texture_revision_ = revision_;
      }
      node->setRect(rect_);
      node->setFiltering(smooth_ ? QSGTexture::Linear : QSGTexture::Nearest);
      return node;
    }

  private:
    QImage image_;
    QRectF rect_;
    bool smooth_{true};
    std::uint64_t revision_{0};
    std::uint64_t texture_revision_{0};
  };

    explicit QuickCanvasItem(QQmlEngine* engine, QQmlContext* context) : QQuickItem() {
      background_ = new Background(this);
      composite_frame_ = new CompositeFrame(this);
      layers_ = new Layers(this);
#ifdef PATCHY_GPU_SHADER_COMPOSITOR
      shader_layers_ = new GpuShaderCompositor(engine, context, this);
#endif
      background_->setZ(0.0);
      composite_frame_->setZ(1.0);
      layers_->setZ(2.0);
#ifdef PATCHY_GPU_SHADER_COMPOSITOR
      shader_layers_->setZ(3.0);
      shader_layers_->setVisible(false);
#endif
      composite_frame_->setVisible(false);
      setFlag(QQuickItem::ItemHasContents, false);
    }

  bool set_document(CanvasGpuDocument document) {
      background_->set_document_rect(document.canvas_rect, document.canvas_backdrop);
      if (!document.composited_frame.isNull()) {
        composite_frame_->set_frame(std::move(document.composited_frame), document.canvas_rect,
                                    document.smooth_scaling);
        composite_frame_->setVisible(true);
        layers_->setVisible(false);
#ifdef PATCHY_GPU_SHADER_COMPOSITOR
        shader_layers_->clear_document();
#endif
        return true;
      }
      composite_frame_->clear_frame();
      composite_frame_->setVisible(false);
      if (document.shader_composition) {
#ifdef PATCHY_GPU_SHADER_COMPOSITOR
        layers_->setVisible(false);
        const auto ready = shader_layers_->set_document(document);
        if (!ready) {
          shader_layers_->clear_document();
          return false;
        }
        return true;
#else
        return false;
#endif
      }
#ifdef PATCHY_GPU_SHADER_COMPOSITOR
      shader_layers_->clear_document();
#endif
      layers_->setVisible(true);
      layers_->set_document(std::move(document));
      return true;
    }

  void clear_document() {
    composite_frame_->clear_frame();
    composite_frame_->setVisible(false);
    layers_->set_document(CanvasGpuDocument{});
#ifdef PATCHY_GPU_SHADER_COMPOSITOR
    shader_layers_->clear_document();
#endif
    background_->set_document_rect({}, Qt::transparent);
  }

protected:
  void geometryChange(const QRectF& new_geometry, const QRectF& old_geometry) override {
    QQuickItem::geometryChange(new_geometry, old_geometry);
    background_->setSize(new_geometry.size());
    composite_frame_->setSize(new_geometry.size());
    layers_->setSize(new_geometry.size());
  }
private:
  Background* background_{nullptr};
  CompositeFrame* composite_frame_{nullptr};
  Layers* layers_{nullptr};
#ifdef PATCHY_GPU_SHADER_COMPOSITOR
  GpuShaderCompositor* shader_layers_{nullptr};
#endif
};

class CanvasGraphicsSurface::OverlayWidget final : public QWidget {
public:
  explicit OverlayWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
  }

  void set_painter(std::function<void(QPainter&, QRect)> painter) {
    painter_ = std::move(painter);
    update();
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(event != nullptr ? event->rect() : rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    if (painter_) {
      painter_(painter, event != nullptr ? event->rect() : rect());
    }
  }

private:
  std::function<void(QPainter&, QRect)> painter_;
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

  quick_item_ = new QuickCanvasItem(quick_widget_->engine(), quick_widget_->rootContext());
  quick_item_->setSize(size());
  quick_widget_->setContent(QUrl(), nullptr, quick_item_);
  overlay_widget_ = new OverlayWidget(this);
  overlay_widget_->setGeometry(rect());
  overlay_widget_->raise();

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
      qInfo().noquote() << "Patchy graphics surface unavailable; using CPU canvas:" << probe.failure_reason;
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

bool CanvasGraphicsSurface::set_gpu_document(CanvasGpuDocument document) {
  if (quick_item_ != nullptr) {
    return quick_item_->set_document(std::move(document));
  }
  return false;
}

void CanvasGraphicsSurface::clear_gpu_document() {
  if (quick_item_ != nullptr) {
    quick_item_->clear_document();
  }
}

void CanvasGraphicsSurface::request_update(const QRegion& region) {
  Q_UNUSED(region);
  if (quick_item_ != nullptr) {
    quick_item_->update();
  }
  if (overlay_widget_ != nullptr) {
    overlay_widget_->update(region);
  }
}

void CanvasGraphicsSurface::set_overlay_painter(std::function<void(QPainter&, QRect)> painter) {
  if (overlay_widget_ != nullptr) {
    overlay_widget_->set_painter(std::move(painter));
  }
}

void CanvasGraphicsSurface::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (quick_widget_ != nullptr) {
    quick_widget_->setGeometry(rect());
  }
  if (overlay_widget_ != nullptr) {
    overlay_widget_->setGeometry(rect());
  }
  if (quick_item_ != nullptr) {
    quick_item_->setSize(size());
  }
}

}  // namespace patchy::ui

#endif
