#pragma once

namespace patchy {

// Clamps a blocking thread fan-out to what can start without lazily spawning
// a pthread when the caller is the wasm main browser thread; returns `wanted`
// unchanged everywhere else (native builds, wasm worker threads).
//
// On Emscripten a pthread only starts on a pre-spawned pool worker while the
// spawning thread is blocked: a lazily spawned worker needs the spawning
// thread to return to the event loop first. A main-thread fan-out that
// exceeds the idle pool and then joins therefore deadlocks the tab (observed
// 2026-08-01: image rotate then merge on a 60 MB PSD wedged the deployed
// build once enough preview workers were still busy; see docs/wasm.md).
// Callers fall back to their sequential path when this returns less than 2.
int max_blocking_fanout_workers(int wanted);

}  // namespace patchy
