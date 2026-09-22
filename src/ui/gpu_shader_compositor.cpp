#include "ui/gpu_shader_compositor.hpp"

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QUrl>
#include <QVariant>
#include <QtGlobal>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace patchy::ui {

class GpuShaderCompositor::TextureItem final : public QQuickItem {
public:
  explicit TextureItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(QQuickItem::ItemHasContents, true);
  }

  void set_image(QImage image, QRectF rect, bool smooth) {
    image_ = std::move(image);
    rect_ = rect;
    smooth_ = smooth;
    ++revision_;
    update();
  }

protected:
  QSGNode* updatePaintNode(QSGNode* old_node, UpdatePaintNodeData*) override {
    auto* node = static_cast<QSGSimpleTextureNode*>(old_node);
    if (node == nullptr) {
      node = new QSGSimpleTextureNode;
    }
    auto* window = this->window();
    if (window == nullptr || image_.isNull()) {
      node->setRect(QRectF());
      return node;
    }
    if (texture_revision_ != revision_ || node->texture() == nullptr) {
      node->setOwnsTexture(false);
      delete node->texture();
      const auto image = image_.format() == QImage::Format_Alpha8
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

GpuShaderCompositor::GpuShaderCompositor(QQmlEngine* engine, QQmlContext* context, QQuickItem* parent)
    : QQuickItem(parent), context_(context) {
  setFlag(QQuickItem::ItemHasContents, false);
  if (engine != nullptr && context_ != nullptr) {
    pass_component_ = std::make_unique<QQmlComponent>(
        engine, QUrl(QStringLiteral("qrc:/patchy/ui/gpu_layer_pass.qml")), this);
    if (pass_component_->isError()) {
      qWarning() << "Patchy GPU compositor QML error:" << pass_component_->errors();
    }
  }
}

GpuShaderCompositor::~GpuShaderCompositor() {
  clear_passes();
}

void GpuShaderCompositor::clear_passes() {
  for (auto* pass : passes_) {
    if (pass != nullptr) {
      pass->setParentItem(nullptr);
      delete pass;
    }
  }
  passes_.clear();
  textures_.clear();
  masks_.clear();
}

QQuickItem* GpuShaderCompositor::create_pass(const CanvasGpuLayer& layer, QQuickItem* backdrop,
                                              bool final_pass) {
  if (pass_component_ == nullptr || !pass_component_->isReady() || context_ == nullptr) {
    return nullptr;
  }
  auto* object = pass_component_->create(context_);
  auto* pass = qobject_cast<QQuickItem*>(object);
  if (pass == nullptr) {
    delete object;
    return nullptr;
  }
  pass->setParentItem(this);
  pass->setSize(size());
  pass->setVisible(final_pass);
  pass->setProperty("backdropSource", QVariant::fromValue(backdrop));
  pass->setProperty("blendMode", layer.blend_mode);
  pass->setProperty("layerOpacity", layer.opacity);
  pass->setProperty("hasMask", layer.has_mask);
  pass->setProperty("maskDefault", layer.mask_default);
  pass->setProperty("maskDensity", layer.mask_density);
  const auto width = std::max<qreal>(1.0, size().width());
  const auto height = std::max<qreal>(1.0, size().height());
  pass->setProperty("maskRect", QRectF(layer.mask_rect.x() / width, layer.mask_rect.y() / height,
                                        layer.mask_rect.width() / width, layer.mask_rect.height() / height));

  auto* source = new TextureItem(pass);
  source->setSize(size());
  source->set_image(layer.image, layer.rect, true);
  textures_.push_back(source);
  pass->setProperty("sourceItem", QVariant::fromValue(static_cast<QQuickItem*>(source)));

  if (layer.has_mask && !layer.mask_image.isNull()) {
    auto* mask = new TextureItem(pass);
    mask->setSize(size());
    mask->set_image(layer.mask_image, layer.mask_rect, false);
    masks_.push_back(mask);
    pass->setProperty("maskItem", QVariant::fromValue(static_cast<QQuickItem*>(mask)));
  } else {
    pass->setProperty("maskItem", QVariant::fromValue(static_cast<QQuickItem*>(nullptr)));
  }
  passes_.push_back(pass);
  return pass;
}

bool GpuShaderCompositor::set_document(const CanvasGpuDocument& document) {
  clear_passes();
  if (document.layers.empty()) {
    setVisible(false);
    return true;
  }
  if (pass_component_ == nullptr || !pass_component_->isReady()) {
    return false;
  }
  QQuickItem* backdrop = nullptr;
  for (std::size_t index = 0; index < document.layers.size(); ++index) {
    auto* pass = create_pass(document.layers[index], backdrop, index + 1U == document.layers.size());
    if (pass == nullptr) {
      clear_passes();
      return false;
    }
    backdrop = pass;
  }
  setVisible(true);
  update();
  return true;
}

void GpuShaderCompositor::clear_document() {
  clear_passes();
  setVisible(false);
}

void GpuShaderCompositor::geometryChange(const QRectF& new_geometry, const QRectF& old_geometry) {
  QQuickItem::geometryChange(new_geometry, old_geometry);
  for (auto* pass : passes_) {
    if (pass != nullptr) {
      pass->setSize(new_geometry.size());
    }
  }
  for (auto* texture : textures_) {
    if (texture != nullptr) {
      texture->setSize(new_geometry.size());
    }
  }
  for (auto* mask : masks_) {
    if (mask != nullptr) {
      mask->setSize(new_geometry.size());
    }
  }
}

}  // namespace patchy::ui
