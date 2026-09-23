#include "ui/webgpu_render_backend.hpp"

#include <utility>

namespace patchy::ui {

WebGpuRenderBackend::WebGpuRenderBackend() : scheduler_(256) {}

bool WebGpuRenderBackend::initialize() {
  if (state_ == patchy::GpuBackendState::Ready) {
    return true;
  }

  QString reason;
  auto compositor = WebGpuDocumentCompositor::create(&reason);
  if (compositor == nullptr) {
    state_ = patchy::GpuBackendState::Failed;
    last_error_ = reason.toStdString();
    if (last_error_.empty()) {
      last_error_ = "Dawn/WebGPU compositor could not be created";
    }
    return false;
  }

  compositor_ = std::move(compositor);
  state_ = patchy::GpuBackendState::Ready;
  last_error_.clear();
  last_submitted_pass_count_ = 0;
  return true;
}

patchy::GpuSubmitResult WebGpuRenderBackend::submit(const patchy::RenderGraph& graph) {
  if (state_ != patchy::GpuBackendState::Ready || compositor_ == nullptr) {
    if (last_error_.empty()) {
      last_error_ = "Dawn/WebGPU backend is not ready";
    }
    return patchy::GpuSubmitResult::BackendError;
  }

  std::string reason;
  const auto order = graph.execution_order(&reason);
  if (order.empty() && !graph.passes().empty()) {
    last_error_ = std::move(reason);
    return patchy::GpuSubmitResult::InvalidGraph;
  }

  last_submitted_pass_count_ = order.size();
  last_error_.clear();
  return patchy::GpuSubmitResult::Submitted;
}

bool WebGpuRenderBackend::recover() {
  if (state_ != patchy::GpuBackendState::Lost && state_ != patchy::GpuBackendState::Failed) {
    return false;
  }

  compositor_.reset();
  state_ = patchy::GpuBackendState::Uninitialized;
  last_error_.clear();
  return initialize();
}

patchy::GpuBackendState WebGpuRenderBackend::state() const noexcept {
  return state_;
}

std::string_view WebGpuRenderBackend::last_error() const noexcept {
  return last_error_;
}

patchy::GpuBackendInfo WebGpuRenderBackend::info() const {
  const auto adapter = adapter_name();
  const auto native_api = native_backend_name();
  std::string name = "Dawn/WebGPU";
  if (!adapter.isEmpty()) {
    name = adapter.toStdString();
  }
  if (!native_api.isEmpty()) {
    name += " via ";
    name += native_api.toStdString();
  }
  return patchy::GpuBackendInfo{std::move(name), compositor_ != nullptr, false};
}

bool WebGpuRenderBackend::compose(const CanvasGpuDocument& document, QImage& output,
                                  QString* failure_reason) {
  if (state_ != patchy::GpuBackendState::Ready || compositor_ == nullptr) {
    const auto reason = last_error_.empty() ? QStringLiteral("Dawn/WebGPU backend is not ready")
                                            : QString::fromStdString(last_error_);
    return fail(patchy::GpuBackendState::Failed, reason, failure_reason);
  }

  const auto width = document.document_size.width();
  const auto height = document.document_size.height();
  if (width <= 0 || height <= 0) {
    return fail(patchy::GpuBackendState::Failed, QStringLiteral("WebGPU document dimensions are invalid"),
                failure_reason);
  }

  const patchy::Rect document_bounds{0, 0, width, height};
  const auto plan = scheduler_.full_plan(document_bounds);
  const auto graph = scheduler_.build_graph(plan);
  if (submit(graph) != patchy::GpuSubmitResult::Submitted) {
    const auto reason = QString::fromStdString(last_error_);
    return fail(patchy::GpuBackendState::Failed,
                reason.isEmpty() ? QStringLiteral("Dawn rejected the render graph") : reason, failure_reason);
  }

  QImage composed;
  QString reason;
  if (!compositor_->compose(document, composed, &reason)) {
    return fail(patchy::GpuBackendState::Lost,
                reason.isEmpty() ? QStringLiteral("Dawn failed the WebGPU document composition") : reason,
                failure_reason);
  }

  output = std::move(composed);
  last_error_.clear();
  return true;
}

QString WebGpuRenderBackend::adapter_name() const {
  return compositor_ != nullptr ? compositor_->adapter_name() : QString{};
}

QString WebGpuRenderBackend::native_backend_name() const {
  return compositor_ != nullptr ? compositor_->native_backend_name() : QString{};
}

std::size_t WebGpuRenderBackend::last_submitted_pass_count() const noexcept {
  return last_submitted_pass_count_;
}

bool WebGpuRenderBackend::fail(patchy::GpuBackendState state, QString reason, QString* failure_reason) {
  state_ = state;
  last_error_ = reason.toStdString();
  if (failure_reason != nullptr) {
    *failure_reason = std::move(reason);
  }
  return false;
}

}  // namespace patchy::ui
