#include "render/gpu_document_capabilities.hpp"

#include "core/document.hpp"
#include "core/layer.hpp"

#include <utility>

namespace patchy {

namespace {

GpuDocumentCapability unsupported(std::string reason) {
  return GpuDocumentCapability{GpuDocumentRenderMode::Unsupported, std::move(reason)};
}

bool separable_shader_blend_mode(BlendMode mode) {
  switch (mode) {
  case BlendMode::Normal:
  case BlendMode::Multiply:
  case BlendMode::Screen:
  case BlendMode::Overlay:
  case BlendMode::Darken:
  case BlendMode::Lighten:
  case BlendMode::ColorDodge:
  case BlendMode::ColorBurn:
  case BlendMode::HardLight:
  case BlendMode::SoftLight:
  case BlendMode::Difference:
  case BlendMode::LinearBurn:
  case BlendMode::PinLight:
  case BlendMode::Exclusion:
  case BlendMode::LinearDodge:
  case BlendMode::Subtract:
  case BlendMode::Divide:
  case BlendMode::VividLight:
  case BlendMode::LinearLight:
  case BlendMode::HardMix:
    return true;
  case BlendMode::PassThrough:
  case BlendMode::Saturation:
  case BlendMode::Luminosity:
  case BlendMode::Hue:
  case BlendMode::Color:
  case BlendMode::DarkerColor:
  case BlendMode::LighterColor:
  case BlendMode::Dissolve:
    return false;
  }
  return false;
}

bool simple_mask_supported(const LayerMask& mask) {
  if (mask.disabled) {
    return true;
  }
  if (mask.feather != 0.0 || mask.pixels.empty()) {
    return mask.pixels.empty();
  }
  return mask.pixels.format() == PixelFormat::gray8() &&
         mask.bounds.width == mask.pixels.width() && mask.bounds.height == mask.pixels.height();
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
  if (!separable_shader_blend_mode(layer.blend_mode())) {
    return unsupported("document contains a non-separable or unsupported blend mode");
  }
  if (layer.mask().has_value() && !simple_mask_supported(*layer.mask())) {
    return unsupported("document contains a feathered or unsupported mask");
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
  const auto blend_if_status = layer.blend_if_payload_status();
  if (blend_if_status == BlendIfPayloadStatus::Unsupported) {
    return unsupported("document contains unsupported Blend If settings");
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
  const auto shader_required = layer.blend_mode() != BlendMode::Normal ||
                               (blend_if_status == BlendIfPayloadStatus::Supported &&
                                !blend_if_is_identity(layer.blend_if())) ||
                               (layer.mask().has_value() && !layer.mask()->disabled);
  return {shader_required ? GpuDocumentRenderMode::PixelStackShader
                          : GpuDocumentRenderMode::PixelStackSourceOver,
          {}};
}

}  // namespace

GpuDocumentCapability gpu_document_capability(const Document& document) {
  if (document.width() <= 0 || document.height() <= 0) {
    return unsupported("document has no renderable canvas");
  }
  auto mode = GpuDocumentRenderMode::PixelStackSourceOver;
  for (const auto& layer : document.layers()) {
    if (!layer.visible() || layer.opacity() <= 0.0F) {
      continue;
    }
    const auto capability = inspect_layer(layer);
    if (!capability.supported()) {
      return capability;
    }
    if (capability.mode == GpuDocumentRenderMode::PixelStackShader) {
      mode = GpuDocumentRenderMode::PixelStackShader;
    }
  }
  return {mode, {}};
}

}  // namespace patchy
