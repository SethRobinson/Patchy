#pragma once

#include "ui/canvas_graphics_surface.hpp"

#include <QQuickItem>
#include <QRectF>
#include <QSizeF>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class QQmlComponent;
class QQmlContext;
class QQmlEngine;

namespace patchy::ui {

// Cross-backend shader compositor. It deliberately uses Qt Quick ShaderEffect
// passes rather than GL/D3D/Metal/Vulkan calls: qt6_add_shaders packages one
// source shader as SPIR-V, GLSL, HLSL, and MSL, and the active Qt RHI consumes
// the representation appropriate for the runtime graphics API.
class GpuShaderCompositor final : public QQuickItem {
public:
  GpuShaderCompositor(QQmlEngine* engine, QQmlContext* context, QQuickItem* parent = nullptr);
  ~GpuShaderCompositor() override;

  [[nodiscard]] bool set_document(const CanvasGpuDocument& document);
  void clear_document();

protected:
  void geometryChange(const QRectF& new_geometry, const QRectF& old_geometry) override;

  // Number of ShaderEffect passes created since construction (for tests and
  // diagnostics). Updates that only change layer content or properties reuse
  // the existing passes and do not increase it.
  [[nodiscard]] std::uint64_t passes_created() const noexcept { return passes_created_; }

private:
  class TextureItem;

  // One ShaderEffect pass with its source and optional mask texture items.
  // Passes are reused across set_document calls: the pass chain only depends
  // on the layer count and order, while per-layer properties and textures are
  // refreshed in place. Rebuilding every pass per update re-created QML
  // objects and re-uploaded every texture on otherwise inexpensive redraws.
  struct Pass {
    QQuickItem* item{nullptr};
    TextureItem* source{nullptr};
    TextureItem* mask{nullptr};
    QRectF mask_rect;  // widget space; normalized against size() for the shader
  };

  void clear_passes();
  void apply_mask_rect(Pass& pass, const QSizeF& size);
  [[nodiscard]] bool ensure_pass_count(std::size_t count);
  void update_pass(Pass& pass, const CanvasGpuLayer& layer, QQuickItem* backdrop, bool final_pass);

  QQmlContext* context_{nullptr};
  std::unique_ptr<QQmlComponent> pass_component_;
  std::vector<Pass> passes_;
  std::uint64_t passes_created_{0};
};

}  // namespace patchy::ui
