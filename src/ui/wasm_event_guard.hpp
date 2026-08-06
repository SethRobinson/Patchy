#pragma once

namespace patchy::ui {

// Defuses a re-entrancy bug in Qt 6.10's wasm suspend-resume event queue
// (QWasmSuspendResumeControl::sendPendingEvents snapshots the queue length,
// then shift()s that many times; a handler that nests an event loop drains
// the same queue, and the outer call's stale count then shift()s an empty
// queue - reading ["index"] of undefined throws through the resumed Asyncify
// stack and permanently parks the tab). The guard registers a no-op handler
// with the control and patches the page-side queue's shift() to return a
// {index: noop} sentinel instead of undefined, so overshoot calls dispatch
// to the no-op. Call once after QApplication construction; wasm builds only
// (the TU compiles under EMSCRIPTEN). See docs/wasm-input.md.
void install_wasm_pending_event_queue_guard();

}  // namespace patchy::ui
