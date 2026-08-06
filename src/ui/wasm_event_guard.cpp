// Guard for Qt 6.10's wasm pending-event queue (see wasm_event_guard.hpp and
// docs/wasm-input.md). Reproduced failure without it: draw on a slow document so
// the release commit nests a processing wait while more input sits in the
// same sendPendingEvents batch; the outer batch loop then shift()s an empty
// queue and the tab dies with one console error ("Cannot read properties of
// undefined (reading 'index')"). The queue object and its consumers live in
// qtbase's qwasmsuspendresumecontrol.cpp; the private header is the only way
// to register a handler index the C++ side will accept, so this TU pins a
// private QtCore API on purpose. Remove once Qt re-reads the live queue
// length per iteration.

#include "ui/wasm_event_guard.hpp"

#include <QtCore/private/qwasmsuspendresumecontrol_p.h>

#include <emscripten.h>
#include <emscripten/val.h>

// shift() keeps real Array semantics (the wasm plugin's page-side handlers
// push() into the same array) except that draining past empty yields the
// no-op sentinel instead of undefined.
EM_JS(void, patchy_js_install_pending_event_queue_guard, (int noop_index), {
  const control = Module.qtSuspendResumeControl;
  if (!control || !Array.isArray(control.pendingEvents) ||
      control.pendingEvents.__patchyShiftGuard) {
    return;
  }
  const queue = control.pendingEvents;
  queue.__patchyShiftGuard = true;
  queue.shift = function() {
    const event = Array.prototype.shift.call(this);
    return event === undefined ? { index: noop_index, arg: undefined } : event;
  };
});

namespace patchy::ui {

void install_wasm_pending_event_queue_guard() {
  auto* control = QWasmSuspendResumeControl::get();
  if (control == nullptr) {
    return;
  }
  static const auto noop_index = control->registerEventHandler([](emscripten::val) {});
  patchy_js_install_pending_event_queue_guard(static_cast<int>(noop_index));
}

}  // namespace patchy::ui
