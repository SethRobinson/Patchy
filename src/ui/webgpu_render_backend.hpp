#pragma once

#include "render/gpu_render_backend.hpp"
#include "render/gpu_tile_scheduler.hpp"
#include "ui/webgpu_document_compositor.hpp"

#include <QImage>
#include <QString>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace patchy::ui {

// Bridges the Qt-free render-graph contract to the existing Dawn compositor.
// The graph is validated as the scheduling boundary; the current Dawn
// compositor still executes one complete-document compute composition and one
// readback. It never exposes a partially rendered frame to Qt Quick.
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

  // Executes the current all-or-nothing Dawn compositor after the graph gate
  // accepts a full-document plan. Failure leaves output untouched.
  [[nodiscard]] bool compose(const CanvasGpuDocument& document, QImage& output,
                             QString* failure_reason = nullptr);

  [[nodiscard]] QString adapter_name() const;
  [[nodiscard]] QString native_backend_name() const;
  [[nodiscard]] std::size_t last_submitted_pass_count() const noexcept;

private:
  bool fail(patchy::GpuBackendState state, QString reason, QString* failure_reason = nullptr);

  std::unique_ptr<WebGpuDocumentCompositor> compositor_;
  patchy::GpuTileScheduler scheduler_;
  patchy::GpuBackendState state_{patchy::GpuBackendState::Uninitialized};
  std::string last_error_;
  std::size_t last_submitted_pass_count_{0};
};

}  // namespace patchy::ui
