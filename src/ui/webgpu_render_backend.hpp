#pragma once

#include "render/gpu_render_backend.hpp"
#include "render/gpu_tile_scheduler.hpp"
#include "ui/webgpu_document_compositor.hpp"

#include <QImage>
#include <QRegion>
#include <QString>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace patchy::ui {

// Bridges the Qt-free render-graph contract to the existing Dawn compositor.
// The graph is validated as the scheduling boundary; Dawn executes the
// accepted full or dirty mip-0 tiles and never exposes a partially rendered
// frame to Qt Quick.
class WebGpuRenderBackend final : public patchy::GpuRenderBackend {
public:
  WebGpuRenderBackend();
  ~WebGpuRenderBackend() override = default;

  WebGpuRenderBackend(const WebGpuRenderBackend&) = delete;
  WebGpuRenderBackend& operator=(const WebGpuRenderBackend&) = delete;

  bool initialize() override;
  [[nodiscard]] patchy::GpuSubmitResult submit(const patchy::RenderGraph& graph) override;
  bool recover() override;

  [[nodiscard]] patchy::GpuBackendState state() const noexcept override;
  [[nodiscard]] std::string_view last_error() const noexcept override;
  [[nodiscard]] patchy::GpuBackendInfo info() const override;

  // Executes the all-or-nothing Dawn compositor after the graph gate accepts a
  // full-document plan. Failure leaves output untouched.
  [[nodiscard]] bool compose(const CanvasGpuDocument& document, QImage& output,
                             QString* failure_reason = nullptr);

  // Rebuilds only tiles intersecting dirty_document_region and reuses the
  // previous frame for all other tiles. The previous frame is never exposed if
  // any requested tile fails.
  [[nodiscard]] bool compose_incremental(const CanvasGpuDocument& document,
                                          const QRegion& dirty_document_region,
                                          const QImage& previous_frame, QImage& output,
                                          QString* failure_reason = nullptr);

  [[nodiscard]] QString adapter_name() const;
  [[nodiscard]] QString native_backend_name() const;
  [[nodiscard]] std::size_t last_submitted_pass_count() const noexcept;
  [[nodiscard]] std::size_t last_rendered_tile_count() const noexcept;
  [[nodiscard]] std::size_t last_readback_bytes() const noexcept;
  [[nodiscard]] WebGpuCompositionMetrics last_composition_metrics() const noexcept;
  [[nodiscard]] DawnVulkanInteropObservation vulkan_interop_observation() const noexcept;

private:
  bool fail(patchy::GpuBackendState state, QString reason, QString* failure_reason = nullptr);

  std::unique_ptr<WebGpuDocumentCompositor> compositor_;
  patchy::GpuTileScheduler scheduler_;
  patchy::GpuBackendState state_{patchy::GpuBackendState::Uninitialized};
  std::string last_error_;
  std::size_t last_submitted_pass_count_{0};
  std::size_t last_rendered_tile_count_{0};
  std::size_t last_readback_bytes_{0};
  WebGpuCompositionMetrics last_composition_metrics_{};
};

}  // namespace patchy::ui
