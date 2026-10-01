#include "ui/gpu_shader_compositor.hpp"

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QUrl>
#include <QVariant>
#include <QVector4D>
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
    // The snapshot hands out implicitly shared images from the layer image
    // cache, so an unchanged layer arrives with the same cacheKey and keeps
    // its uploaded texture; only placement and filtering are refreshed.
    if (image_.isNull() != image.isNull() || image_.cacheKey() != image.cacheKey()) {
      ++revision_;
    }
    image_ = std::move(image);
    rect_ = rect;
    smooth_ = smooth;
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
      // Grayscale8 masks become opaque RGBA with the mask value in every colour
      // channel; the shader reads coverage from red. Colour layers stay
      // premultiplied for the blend passes.
      const auto image = image_.format() == QImage::Format_Grayscale8
                             ? image_.convertToFormat(QImage::Format_RGBA8888)
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
  for (auto& pass : passes_) {
    if (pass.item != nullptr) {
      pass.item->setParentItem(nullptr);
      delete pass.item;  // owns its TextureItem children
    }
  }
  passes_.clear();
}

bool GpuShaderCompositor::ensure_pass_count(std::size_t count) {
  if (pass_component_ == nullptr || !pass_component_->isReady() || context_ == nullptr) {
    return false;
  }
  while (passes_.size() > count) {
    auto& last = passes_.back();
    if (last.item != nullptr) {
      last.item->setParentItem(nullptr);
      delete last.item;
    }
    passes_.pop_back();
  }
  while (passes_.size() < count) {
    auto* object = pass_component_->create(context_);
    auto* item = qobject_cast<QQuickItem*>(object);
    if (item == nullptr) {
      delete object;
      return false;
    }
    item->setParentItem(this);
    item->setSize(size());
    Pass pass;
    pass.item = item;
    pass.source = new TextureItem(item);
    pass.source->setSize(size());
    item->setProperty("sourceItem", QVariant::fromValue(static_cast<QQuickItem*>(pass.source)));
    item->setProperty("maskItem", QVariant::fromValue(static_cast<QQuickItem*>(nullptr)));
    passes_.push_back(pass);
    ++passes_created_;
  }
  return true;
}

void GpuShaderCompositor::apply_mask_rect(Pass& pass, const QSizeF& size) {
  const auto width = std::max<qreal>(1.0, size.width());
  const auto height = std::max<qreal>(1.0, size.height());
  pass.item->setProperty("maskRect", QRectF(pass.mask_rect.x() / width, pass.mask_rect.y() / height,
                                            pass.mask_rect.width() / width, pass.mask_rect.height() / height));
}

void GpuShaderCompositor::update_pass(Pass& pass, const CanvasGpuLayer& layer, QQuickItem* backdrop,
                                      bool final_pass) {
  auto* item = pass.item;
  item->setVisible(final_pass);
  item->setProperty("backdropSource", QVariant::fromValue(backdrop));
  item->setProperty("blendMode", layer.blend_mode);
  item->setProperty("layerOpacity", layer.opacity);
  item->setProperty("hasMask", layer.has_mask);
  item->setProperty("maskDefault", layer.mask_default);
  item->setProperty("maskDensity", layer.mask_density);
  item->setProperty("hasBlendIf", layer.has_blend_if ? 1.0 : 0.0);
  const auto thresholds = [](const CanvasGpuBlendIfThresholds& value) {
    return QVector4D(static_cast<float>(value.black_low), static_cast<float>(value.black_high),
                     static_cast<float>(value.white_low), static_cast<float>(value.white_high));
  };
  item->setProperty("blendIfGrayThis", thresholds(layer.blend_if[0].this_layer));
  item->setProperty("blendIfRedThis", thresholds(layer.blend_if[1].this_layer));
  item->setProperty("blendIfGreenThis", thresholds(layer.blend_if[2].this_layer));
  item->setProperty("blendIfBlueThis", thresholds(layer.blend_if[3].this_layer));
  item->setProperty("blendIfGrayUnderlying", thresholds(layer.blend_if[0].underlying_layer));
  item->setProperty("blendIfRedUnderlying", thresholds(layer.blend_if[1].underlying_layer));
  item->setProperty("blendIfGreenUnderlying", thresholds(layer.blend_if[2].underlying_layer));
  item->setProperty("blendIfBlueUnderlying", thresholds(layer.blend_if[3].underlying_layer));
  pass.mask_rect = layer.mask_rect;
  apply_mask_rect(pass, size());

  pass.source->set_image(layer.image, layer.rect, true);

  const bool wants_mask = layer.has_mask && !layer.mask_image.isNull();
  if (wants_mask && pass.mask == nullptr) {
    pass.mask = new TextureItem(item);
    pass.mask->setSize(size());
    item->setProperty("maskItem", QVariant::fromValue(static_cast<QQuickItem*>(pass.mask)));
  } else if (!wants_mask && pass.mask != nullptr) {
    item->setProperty("maskItem", QVariant::fromValue(static_cast<QQuickItem*>(nullptr)));
    pass.mask->setParentItem(nullptr);
    delete pass.mask;
    pass.mask = nullptr;
  }
  if (pass.mask != nullptr) {
    pass.mask->set_image(layer.mask_image, layer.mask_rect, false);
  }
}

bool GpuShaderCompositor::set_document(const CanvasGpuDocument& document) {
  if (document.layers.empty()) {
    clear_passes();
    setVisible(false);
    return true;
  }
  if (!ensure_pass_count(document.layers.size())) {
    clear_passes();
    return false;
  }
  QQuickItem* backdrop = nullptr;
  for (std::size_t index = 0; index < document.layers.size(); ++index) {
    update_pass(passes_[index], document.layers[index], backdrop, index + 1U == document.layers.size());
    backdrop = passes_[index].item;
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
  for (auto& pass : passes_) {
    if (pass.item != nullptr) {
      pass.item->setSize(new_geometry.size());
      // The shader samples the mask through normalized coordinates, so the
      // rectangle must be re-normalized whenever the item size changes.
      apply_mask_rect(pass, new_geometry.size());
    }
    if (pass.source != nullptr) {
      pass.source->setSize(new_geometry.size());
    }
    if (pass.mask != nullptr) {
      pass.mask->setSize(new_geometry.size());
    }
  }
}

}  // namespace patchy::ui
