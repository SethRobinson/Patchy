#pragma once

#include "ui/canvas_graphics_surface.hpp"

#include <QImage>
#include <QString>

#include <memory>

namespace patchy::ui {

// Dawn is an optional document-composition backend. Qt Quick remains the
// presentation backend because Qt RHI does not expose WebGPU as a GraphicsApi.
// The compositor returns a CPU-readable frame so the existing CanvasGraphicsSurface
// can present it without introducing a second windowing or input path.
class WebGpuDocumentCompositor final {
public:
  static std::unique_ptr<WebGpuDocumentCompositor> create(QString* failure_reason = nullptr);
  static bool should_try_automatically();

  ~WebGpuDocumentCompositor();

  WebGpuDocumentCompositor(const WebGpuDocumentCompositor&) = delete;
  WebGpuDocumentCompositor& operator=(const WebGpuDocumentCompositor&) = delete;

  [[nodiscard]] bool available() const noexcept;
  [[nodiscard]] QString adapter_name() const;
  [[nodiscard]] QString native_backend_name() const;

  // Composes the complete supported document on the WebGPU device. The
  // all-or-nothing contract is intentional: a failed composition never mixes
  // partially composed GPU layers with the CPU reference compositor.
  [[nodiscard]] bool compose(const CanvasGpuDocument& document, QImage& output, QString* failure_reason = nullptr);

private:
  explicit WebGpuDocumentCompositor(void* implementation);

  void* implementation_{nullptr};
};

}  // namespace patchy::ui
