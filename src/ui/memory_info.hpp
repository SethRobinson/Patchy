#pragma once

#include <QtGlobal>

#include <cstddef>

namespace patchy::ui {

// Process/system memory probes shared by the About dialog, the stress-test
// report, and the history byte budget. Every probe returns -1 when the
// platform offers no equivalent.

// Total physical RAM. Windows/Linux/macOS; -1 on wasm.
[[nodiscard]] qint64 total_physical_ram_mb();

// Current process memory: Windows working set, Linux VmRSS, macOS task_info
// resident size, wasm the selected allocator's live claim on the heap (see
// memory_info.cpp).
[[nodiscard]] qint64 current_process_memory_mb();

// Peak process memory: Windows peak working set, Linux VmHWM, macOS getrusage
// ru_maxrss, wasm the running peak of the allocator claim. The wasm peak is
// best-effort: it only refreshes when something probes (the About dialog's
// timer, the stress report, or the opt-in memtest telemetry).
[[nodiscard]] qint64 peak_process_memory_mb();

// Size of the wasm linear-memory buffer (WebAssembly.Memory), the number the
// browser's tab memory accounting sees. Grows when the allocator overruns it
// and never shrinks, so it is the permanent high-water mark, not usage.
// -1 off wasm.
[[nodiscard]] qint64 wasm_heap_reserved_mb();

// The effective wasm heap ceiling: the WebAssembly.Memory maximum chosen by
// the shell page (published as globalThis.patchyWasmMemoryMaximumBytes),
// falling back to Emscripten's baked-in maximum when the page did not choose
// (e.g. Qt's dev-loop patchy.html). -1 off wasm.
[[nodiscard]] qint64 wasm_heap_limit_mb();

// Total byte budget for undo/redo history across all document sessions (the
// COW-aware marginal bytes history retains beyond the live documents). Small
// and fixed on wasm; a fraction of physical RAM on desktop. The env override
// PATCHY_HISTORY_BUDGET_TEST_MB (read on every call) exists for tests.
[[nodiscard]] std::size_t history_memory_budget_bytes();

}  // namespace patchy::ui
