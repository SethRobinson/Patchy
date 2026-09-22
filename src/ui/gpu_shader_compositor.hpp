#pragma once

#include "ui/canvas_graphics_surface.hpp"

#include <QQuickItem>
#include <QSizeF>

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

private:
  class TextureItem;

  void clear_passes();
  [[nodiscard]] QQuickItem* create_pass(const CanvasGpuLayer& layer, QQuickItem* backdrop,
                                        bool final_pass);

  QQmlContext* context_{nullptr};
  std::unique_ptr<QQmlComponent> pass_component_;
  std::vector<QQuickItem*> passes_;
  std::vector<TextureItem*> textures_;
  std::vector<TextureItem*> masks_;
};

}  // namespace patchy::ui
