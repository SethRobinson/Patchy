#pragma once

namespace patchy {

// Clamps a blocking thread fan-out to what can start without lazily spawning
// a pthread when the caller is the wasm main browser thread; returns `wanted`
// unchanged everywhere else (native builds, wasm worker threads outside a
// BlockingFanoutBudgetScope).
//
// On Emscripten a pthread only starts on a pre-spawned pool worker while the
// spawning thread is blocked: a lazily spawned worker needs the spawning
// thread to return to the event loop first. A main-thread fan-out that
// exceeds the idle pool and then joins therefore deadlocks the tab (observed
// 2026-08-01: image rotate then merge on a 60 MB PSD wedged the deployed
// build once enough preview workers were still busy; see docs/wasm.md).
// Callers fall back to their sequential path when this returns less than 2.
int max_blocking_fanout_workers(int wanted);

// Number of pre-spawned pthread pool workers currently idle, read on the wasm
// main browser thread. Returns a huge value on native builds, on
// single-threaded wasm, and on wasm worker threads (which cannot see the
// pool: PThread.unusedWorkers lives in the main thread's JS scope).
int idle_prespawned_pool_workers();

// Published by the wasm main thread around "launch a compute on a worker and
// wait for it" blocks. While a scope is active, max_blocking_fanout_workers
// ALSO clamps worker-thread callers to the budget, so the awaited compute's
// own fan-outs stay within the pre-spawned pool instead of relying on lazy
// Worker spawns. Lazy spawns are unreliable exactly then: a finished
// pthread's worker only returns to the pool when the main thread's JS event
// loop runs its 'cleanupThread' message, so back-to-back fan-outs inside one
// compute (free-transform release: resample then patch render) always
// overrun the pool while the main thread waits (see docs/wasm.md). No-op on
// native builds. Scopes nest (inner value wins, restored on destruction);
// only the main thread creates them. A negative budget constructs an inert
// scope that publishes nothing, so call sites can build one unconditionally.
class BlockingFanoutBudgetScope {
public:
  explicit BlockingFanoutBudgetScope(int budget);
  ~BlockingFanoutBudgetScope();
  BlockingFanoutBudgetScope(const BlockingFanoutBudgetScope&) = delete;
  BlockingFanoutBudgetScope& operator=(const BlockingFanoutBudgetScope&) = delete;

private:
  int previous_budget_;
};

}  // namespace patchy
