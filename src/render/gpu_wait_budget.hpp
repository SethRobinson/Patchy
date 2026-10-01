#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace patchy {

// Upper bounds for how long the GPU document compositor may block the calling
// thread while waiting on asynchronous device work. The canvas path calls the
// compositor synchronously, so an unbounded wait would freeze the UI until the
// driver answers; these budgets turn a stalled driver into an ordinary
// composition failure that the caller handles with the CPU fallback.
struct GpuWaitBudget {
  // Adapter and device requests during (re)initialization.
  std::chrono::milliseconds startup{2000};
  // All queue and readback waits of one composition combined.
  std::chrono::milliseconds frame{250};
};

constexpr std::int64_t kGpuWaitBudgetMaxMs = 60000;

// Parses an optional millisecond override. Empty, non-numeric or negative
// values keep `fallback`; values above kGpuWaitBudgetMaxMs are clamped. Zero
// is accepted and means "expire immediately", which tests use to exercise the
// timeout path without a real stalled device.
[[nodiscard]] std::chrono::milliseconds gpu_wait_budget_from_text(const std::optional<std::string>& text,
                                                                 std::chrono::milliseconds fallback) noexcept;

// Builds the budget from PATCHY_WEBGPU_STARTUP_BUDGET_MS and
// PATCHY_WEBGPU_FRAME_BUDGET_MS (both optional).
[[nodiscard]] GpuWaitBudget gpu_wait_budget_from_environment();

// A monotonic deadline shared by every wait of one operation.
class GpuWaitDeadline final {
public:
  explicit GpuWaitDeadline(std::chrono::milliseconds budget) noexcept;
  [[nodiscard]] bool expired() const noexcept;
  [[nodiscard]] std::chrono::milliseconds budget() const noexcept { return budget_; }

private:
  std::chrono::milliseconds budget_;
  std::chrono::steady_clock::time_point deadline_;
};

}  // namespace patchy
