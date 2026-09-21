#include "render/gpu_document_capabilities.hpp"

#include "core/document.hpp"
#include "core/layer.hpp"

#include <utility>

namespace patchy {

namespace {

GpuDocumentCapability unsupported(std::string reason) {
  return GpuDocumentCapability{GpuDocumentRenderMode::Unsupported, std::move(reason)};
}

GpuDocumentCapability inspect_layer(const Layer& layer) {
  if (layer.kind() != LayerKind::Pixel) {
    return unsupported("document contains a non-pixel layer");
  }
  if (!layer.visible() || layer.opacity() <= 0.0F) {
    return {GpuDocumentRenderMode::PixelStackSourceOver, {}};
  }
  if (layer.clipped()) {
    return unsupported("document contains a clipped layer");
  }
  if (layer.fill_opacity() != 1.0F) {
    return unsupported("document contains fill opacity");
  }
  if (layer.blend_mode() != BlendMode::Normal) {
    return unsupported("document contains a non-Normal blend mode");
  }
  if (layer.mask().has_value()) {
    return unsupported("document contains a raster mask");
  }
  if (!layer.layer_style().empty()) {
    return unsupported("document contains a layer style");
  }
  if (layer.smart_filter_stack() != nullptr) {
    return unsupported("document contains smart filters");
  }
  if (layer.vector_shape() != nullptr || layer.vector_mask() != nullptr) {
    return unsupported("document contains vector layer content");
  }
  if (layer.blend_if_payload_status() != BlendIfPayloadStatus::Empty) {
    return unsupported("document contains Blend If settings");
  }
  if (!layer.channel_restriction_supported() || layer.restricted_channels() != 0) {
    return unsupported("document contains channel restrictions");
  }
  const auto& pixels = layer.pixels();
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 ||
      (pixels.format().channels != 3U && pixels.format().channels != 4U)) {
    return unsupported("document contains an unsupported pixel format");
  }
  if (layer.bounds().width != pixels.width() || layer.bounds().height != pixels.height()) {
    return unsupported("layer bounds do not match its pixel buffer");
  }
  return {GpuDocumentRenderMode::PixelStackSourceOver, {}};
}

}  // namespace

GpuDocumentCapability gpu_document_capability(const Document& document) {
  if (document.width() <= 0 || document.height() <= 0) {
    return unsupported("document has no renderable canvas");
  }
  for (const auto& layer : document.layers()) {
    if (!layer.visible() || layer.opacity() <= 0.0F) {
      continue;
    }
    const auto capability = inspect_layer(layer);
    if (!capability.supported()) {
      return capability;
    }
  }
  return {GpuDocumentRenderMode::PixelStackSourceOver, {}};
}

}  // namespace patchy
