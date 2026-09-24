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
  last_rendered_tile_count_ = 0;
  last_readback_bytes_ = 0;
  last_composition_metrics_ = {};
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
  last_submitted_pass_count_ = 0;
  last_rendered_tile_count_ = 0;
  last_readback_bytes_ = 0;
  last_composition_metrics_ = {};
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
  last_rendered_tile_count_ = 0;
  last_readback_bytes_ = 0;
  last_composition_metrics_ = {};
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

  last_rendered_tile_count_ = plan.tiles.size();
  last_readback_bytes_ = 0;
  for (const auto key : plan.tiles) {
    const auto rect = plan.tile_rect(key);
    if (!rect.empty()) {
      const auto padded_row_bytes = ((static_cast<std::size_t>(rect.width) * 4U + 255U) / 256U) * 256U;
      last_readback_bytes_ += padded_row_bytes * static_cast<std::size_t>(rect.height);
    }
  }

  QImage composed;
  QString reason;
  if (!compositor_->compose_tiles(document, plan, nullptr, composed, &reason)) {
    return fail(patchy::GpuBackendState::Lost,
                reason.isEmpty() ? QStringLiteral("Dawn failed the WebGPU document composition") : reason,
                failure_reason);
  }

  output = std::move(composed);
  last_composition_metrics_ = compositor_->last_metrics();
  last_error_.clear();
  return true;
}

bool WebGpuRenderBackend::compose_incremental(const CanvasGpuDocument& document,
                                               const QRegion& dirty_document_region,
                                               const QImage& previous_frame, QImage& output,
                                               QString* failure_reason) {
  last_rendered_tile_count_ = 0;
  last_readback_bytes_ = 0;
  last_composition_metrics_ = {};
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
  const bool previous_valid = previous_frame.size() == document.document_size && !previous_frame.isNull();
  patchy::GpuTileRenderPlan plan;
  if (!previous_valid) {
    plan = scheduler_.full_plan(document_bounds);
  } else {
    patchy::DirtyRegionSet dirty;
    for (const auto& document_rect : dirty_document_region) {
      const auto clipped = document_rect.intersected(QRect(0, 0, width, height));
      if (!clipped.isEmpty()) {
        dirty.add(patchy::Rect{clipped.x(), clipped.y(), clipped.width(), clipped.height()});
      }
    }
    if (dirty.empty()) {
      output = previous_frame;
      last_submitted_pass_count_ = 0;
      last_rendered_tile_count_ = 0;
      last_readback_bytes_ = 0;
      last_composition_metrics_ = {};
      last_error_.clear();
      return true;
    }
    plan = scheduler_.dirty_plan(document_bounds, dirty);
  }

  const auto graph = scheduler_.build_graph(plan);
  if (submit(graph) != patchy::GpuSubmitResult::Submitted) {
    const auto reason = QString::fromStdString(last_error_);
    return fail(patchy::GpuBackendState::Failed,
                reason.isEmpty() ? QStringLiteral("Dawn rejected the dirty render graph") : reason,
                failure_reason);
  }

  last_rendered_tile_count_ = plan.tiles.size();
  last_readback_bytes_ = 0;
  for (const auto key : plan.tiles) {
    const auto rect = plan.tile_rect(key);
    if (!rect.empty()) {
      const auto padded_row_bytes = ((static_cast<std::size_t>(rect.width) * 4U + 255U) / 256U) * 256U;
      last_readback_bytes_ += padded_row_bytes * static_cast<std::size_t>(rect.height);
    }
  }

  QImage composed;
  QString reason;
  if (!compositor_->compose_tiles(document, plan, previous_valid ? &previous_frame : nullptr, composed, &reason)) {
    return fail(patchy::GpuBackendState::Lost,
                reason.isEmpty() ? QStringLiteral("Dawn failed the dirty WebGPU tile composition") : reason,
                failure_reason);
  }
  output = std::move(composed);
  last_composition_metrics_ = compositor_->last_metrics();
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

std::size_t WebGpuRenderBackend::last_rendered_tile_count() const noexcept {
  return last_rendered_tile_count_;
}

std::size_t WebGpuRenderBackend::last_readback_bytes() const noexcept {
  return last_readback_bytes_;
}

WebGpuCompositionMetrics WebGpuRenderBackend::last_composition_metrics() const noexcept {
  return last_composition_metrics_;
}

DawnVulkanInteropObservation WebGpuRenderBackend::vulkan_interop_observation() const noexcept {
  return compositor_ != nullptr ? compositor_->vulkan_interop_observation()
                                : DawnVulkanInteropObservation{};
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
