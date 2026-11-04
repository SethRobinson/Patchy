#include "core/worker_budget.hpp"

#include "core/environment.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <limits>
#include <thread>

#if defined(__EMSCRIPTEN_PTHREADS__)
#include <emscripten.h>
#include <emscripten/threading.h>
#endif

namespace patchy {

namespace {

// -1 = no scope active. Written by the main thread (BlockingFanoutBudgetScope),
// read by worker threads inside max_blocking_fanout_workers.
std::atomic<int> blocking_fanout_budget{-1};

// -1 = no scope active on this thread (ThreadFanoutBudgetScope).
thread_local int thread_fanout_budget = -1;

// A BlockingFanoutBudgetScope publishes only from the thread that owns the
// process-wide value: the wasm main browser thread. Native builds never read
// the budget, so any thread may publish there (the CPU-only tests do).
bool may_publish_blocking_budget() {
#if defined(__EMSCRIPTEN_PTHREADS__)
  return emscripten_is_main_browser_thread();
#else
  return true;
#endif
}

}  // namespace

int idle_prespawned_pool_workers() {
#if defined(__EMSCRIPTEN_PTHREADS__)
  if (emscripten_is_main_browser_thread()) {
    return EM_ASM_INT({
      return (typeof PThread === 'undefined' || !PThread.unusedWorkers) ? 0 : PThread.unusedWorkers.length;
    });
  }
#endif
  return std::numeric_limits<int>::max();
}

int hardware_worker_threads() noexcept {
  static const int threads = [] {
    const int hardware = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    const auto override_value = environment_variable("PATCHY_RENDER_THREADS");
    if (!override_value.has_value()) {
      return hardware;
    }
    char* end = nullptr;
    const long parsed = std::strtol(override_value->c_str(), &end, 10);
    if (end == override_value->c_str() || parsed < 1) {
      return hardware;
    }
    return static_cast<int>(std::min<long>(parsed, 256));
  }();
  return threads;
}

int max_blocking_fanout_workers(int wanted) {
#if defined(__EMSCRIPTEN_PTHREADS__)
  if (emscripten_is_main_browser_thread()) {
    // PThread.unusedWorkers holds the pre-spawned pool workers not currently
    // running a pthread. Two workers of margin absorb spawns racing in from
    // worker threads between this read and the fan-out's own pthread_create
    // calls (main-thread spawns cannot interleave; this thread does not
    // yield between the read and the creates).
    const int idle = idle_prespawned_pool_workers();
    return std::clamp(idle - 2, 0, wanted);
  }
  const int budget = current_blocking_fanout_budget();
  if (budget >= 0) {
    return std::min(wanted, budget);
  }
#endif
  return wanted;
}

int current_blocking_fanout_budget() {
  const int published = blocking_fanout_budget.load(std::memory_order_relaxed);
  const int local = thread_fanout_budget;
  if (published < 0) {
    return local;
  }
  return local < 0 ? published : std::min(published, local);
}

// A negative budget, or a wasm worker thread, constructs an inert scope
// (nothing published or restored), so call sites can build one unconditionally.
BlockingFanoutBudgetScope::BlockingFanoutBudgetScope(int budget)
    : previous_budget_(-1), published_(budget >= 0 && may_publish_blocking_budget()) {
  if (published_) {
    previous_budget_ = blocking_fanout_budget.exchange(budget, std::memory_order_relaxed);
  }
}

BlockingFanoutBudgetScope::~BlockingFanoutBudgetScope() {
  if (published_) {
    blocking_fanout_budget.store(previous_budget_, std::memory_order_relaxed);
  }
}

ThreadFanoutBudgetScope::ThreadFanoutBudgetScope(int budget) : previous_budget_(thread_fanout_budget) {
  if (budget >= 0) {
    thread_fanout_budget = previous_budget_ < 0 ? budget : std::min(previous_budget_, budget);
  }
}

ThreadFanoutBudgetScope::~ThreadFanoutBudgetScope() {
  thread_fanout_budget = previous_budget_;
}

}  // namespace patchy
