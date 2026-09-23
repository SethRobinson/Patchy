#pragma once

#include "render/gpu_tile_scheduler.hpp"
#include "ui/canvas_graphics_surface.hpp"

#include <QImage>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace patchy::ui {

struct WebGpuCompositionMetrics {
  std::size_t source_upload_bytes{0};
  std::size_t mask_upload_bytes{0};
  std::size_t clear_upload_bytes{0};
  std::size_t source_texture_reuses{0};
  std::size_t mask_texture_reuses{0};
  std::size_t scratch_texture_reuses{0};
  std::size_t uniform_buffer_reuses{0};
  std::size_t readback_buffer_reuses{0};
  std::size_t bind_group_reuses{0};
  std::size_t queue_submissions{0};
  std::size_t queue_waits{0};
  std::uint64_t composition_time_ns{0};
};

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

  // Executes only the mip-0 tiles in `plan`. When `previous_frame` has the
  // document dimensions, untouched tiles are copied from it and the output is
  // committed only after every requested tile has been read back successfully.
  [[nodiscard]] bool compose_tiles(const CanvasGpuDocument& document,
                                   const patchy::GpuTileRenderPlan& plan,
                                   const QImage* previous_frame, QImage& output,
                                   QString* failure_reason = nullptr);
  [[nodiscard]] WebGpuCompositionMetrics last_metrics() const noexcept;

private:
  explicit WebGpuDocumentCompositor(void* implementation);

  void* implementation_{nullptr};
};

}  // namespace patchy::ui
