#pragma once

#include "render/gpu_render_backend.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace patchy::test {

class FakeGpuBackend final : public GpuRenderBackend {
public:
  bool initialize() override {
    if (state_ == GpuBackendState::Ready) {
      return true;
    }
    state_ = GpuBackendState::Ready;
    return true;
  }

  GpuSubmitResult submit(const RenderGraph& graph) override {
    if (state_ == GpuBackendState::Lost) {
      last_error_ = "fake device is lost";
      return GpuSubmitResult::DeviceLost;
    }
    if (state_ != GpuBackendState::Ready) {
      last_error_ = "fake backend is not initialized";
      return GpuSubmitResult::BackendError;
    }
    std::string reason;
    const auto order = graph.execution_order(&reason);
    if (order.empty() && !graph.passes().empty()) {
      last_error_ = reason;
      return GpuSubmitResult::InvalidGraph;
    }
    submitted_passes_ = order;
    last_error_.clear();
    return GpuSubmitResult::Submitted;
  }

  bool recover() override {
    if (state_ != GpuBackendState::Lost) {
      return false;
    }
    ++recovery_count_;
    state_ = GpuBackendState::Ready;
    last_error_.clear();
    return true;
  }

  [[nodiscard]] GpuBackendState state() const noexcept override {
    return state_;
  }

  [[nodiscard]] std::string_view last_error() const noexcept override {
    return last_error_;
  }

  [[nodiscard]] GpuBackendInfo info() const override {
    return GpuBackendInfo{"fake-recording-backend", false, false};
  }

  void lose_device(std::string reason = "fake device lost") {
    state_ = GpuBackendState::Lost;
    last_error_ = std::move(reason);
  }

  [[nodiscard]] const std::vector<RenderPassId>& submitted_passes() const noexcept {
    return submitted_passes_;
  }

  [[nodiscard]] std::size_t recovery_count() const noexcept {
    return recovery_count_;
  }

private:
  GpuBackendState state_{GpuBackendState::Uninitialized};
  std::string last_error_;
  std::vector<RenderPassId> submitted_passes_;
  std::size_t recovery_count_{0};
};

}  // namespace patchy::test
