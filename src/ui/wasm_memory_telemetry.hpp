#pragma once

namespace patchy::ui {

class MainWindow;

// Installs the 1 Hz publisher that mirrors the app's memory picture into
// globalThis.patchyMemStats ({heapBytes, usedBytes, peakUsedBytes,
// limitBytes, historyBytes, historyBudgetBytes, seq, timestampMs}) so page JS
// and the Safari/iOS test harnesses can poll it without touching the wasm
// runtime. seq/timestampMs exist for staleness detection: the timer cannot
// tick during a long synchronous compute. DIAGNOSTICS OPT-IN ONLY: it no-ops
// unless ?PATCHY_MEM_STATS=1 (or ?PATCHY_MEM_LOG=1, which also logs each
// sample to the console), so release visitors never run it; the memtest
// harness opts in automatically. Wasm builds only (the TU compiles under
// EMSCRIPTEN); see docs/wasm-memory.md.
void install_wasm_memory_telemetry(MainWindow& window);

}  // namespace patchy::ui
