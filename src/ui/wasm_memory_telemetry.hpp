#pragma once

namespace patchy::ui {

class MainWindow;

// Starts the always-on 1 Hz publisher that mirrors the app's memory picture
// into globalThis.patchyMemStats ({heapBytes, usedBytes, peakUsedBytes,
// limitBytes, historyBytes, historyBudgetBytes, seq, timestampMs}) so page JS
// and the Safari/iOS test harnesses can poll it without touching the wasm
// runtime. seq/timestampMs exist for staleness detection: the timer cannot
// tick during a long synchronous compute. ?PATCHY_MEM_LOG=1 also logs each
// sample to the console. Wasm builds only (the TU compiles under EMSCRIPTEN);
// see docs/wasm.md.
void install_wasm_memory_telemetry(MainWindow& window);

}  // namespace patchy::ui
