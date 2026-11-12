#pragma once

#include "render/gpu_tile_scheduler.hpp"
#include "ui/canvas_graphics_surface.hpp"
#include "ui/vulkan_qt_interop_probe.hpp"

#include <QImage>
#include <QString>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace patchy::ui {

// Packs the four CanvasGpuBlendIfRanges (Gray, Red, Green, Blue) into the
// eight vec4 slots of the WGSL Params uniform, which declares the four
// "this layer" ranges first and the four "underlying" ranges after them:
// slots 0..3 = this (Gray, Red, Green, Blue), slots 4..7 = underlying.
// Each vec4 is {black_low, black_high, white_low, white_high}.
using WebGpuBlendIfUniform = std::array<std::array<float, 4>, 8>;
[[nodiscard]] WebGpuBlendIfUniform pack_webgpu_blend_if_uniform(const std::array<CanvasGpuBlendIfRanges, 4>& ranges);

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
  // Waits that hit the GpuWaitBudget deadline and failed the operation.
  std::size_t wait_timeouts{0};
  std::uint64_t composition_time_ns{0};
};

// Dawn is an optional document-composition backend. Qt Quick remains the
// presentation backend because Qt RHI does not expose WebGPU as a GraphicsApi.
// The compositor returns a CPU-readable frame so the existing CanvasGraphicsSurface
// can present it without introducing a second windowing or input path.
class WebGpuDocumentCompositor final {
public:
  static std::unique_ptr<WebGpuDocumentCompositor> create(QString* failure_reason = nullptr);
  // True only when PATCHY_RENDER_BACKEND (or the PATCHY_GPU_CANVAS alias) is
  // "webgpu" or "gpu". Absent or any other value keeps Dawn unprobed.
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
  [[nodiscard]] DawnVulkanInteropObservation vulkan_interop_observation() const noexcept;

private:
  explicit WebGpuDocumentCompositor(void* implementation);

  void* implementation_{nullptr};
};

}  // namespace patchy::ui
