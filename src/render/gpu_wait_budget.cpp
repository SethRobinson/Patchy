#include "render/gpu_wait_budget.hpp"

#include "core/environment.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace patchy {

std::chrono::milliseconds gpu_wait_budget_from_text(const std::optional<std::string>& text,
                                                    std::chrono::milliseconds fallback) noexcept {
  if (!text.has_value()) {
    return fallback;
  }
  auto begin = text->data();
  const auto* end = begin + text->size();
  while (begin < end && std::isspace(static_cast<unsigned char>(*begin)) != 0) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(*(end - 1))) != 0) {
    --end;
  }
  if (begin == end) {
    return fallback;
  }
  std::int64_t value = 0;
  const auto result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end || value < 0) {
    return fallback;
  }
  return std::chrono::milliseconds(std::min(value, kGpuWaitBudgetMaxMs));
}

GpuWaitBudget gpu_wait_budget_from_environment() {
  GpuWaitBudget budget;
  budget.startup =
      gpu_wait_budget_from_text(environment_variable("PATCHY_WEBGPU_STARTUP_BUDGET_MS"), budget.startup);
  budget.frame = gpu_wait_budget_from_text(environment_variable("PATCHY_WEBGPU_FRAME_BUDGET_MS"), budget.frame);
  return budget;
}

GpuWaitDeadline::GpuWaitDeadline(std::chrono::milliseconds budget) noexcept
    : budget_(budget), deadline_(std::chrono::steady_clock::now() + budget) {}

bool GpuWaitDeadline::expired() const noexcept {
  return std::chrono::steady_clock::now() >= deadline_;
}

}  // namespace patchy
