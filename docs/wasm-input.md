# WebAssembly input, keyboard focus, and the pending-event queue

How browser input reaches Qt in the wasm build, the two failure modes that
follow from it (stolen keyboard focus, the pending-event queue crash), and
the guards Patchy installs. Companion to [wasm.md](wasm.md); read this
before touching input handling, hotkeys, focus, or the processing waits in
the wasm build. Verified against Qt 6.10.3
(`qtbase/src/corelib/platform/wasm/qwasmsuspendresumecontrol.cpp`,
`src/plugins/platforms/wasm/qwasmwindow.cpp`, `qwasminputcontext.cpp`).

## How input is delivered

- Every DOM event handler Qt registers goes through
  `QWasmSuspendResumeControl`: the page-side handler only pushes the event
  into `Module.qtSuspendResumeControl.pendingEvents` and resolves the
  suspended instance's resume promise. The C++ handler runs later, when
  `sendPendingEvents()` drains the queue.
- When the app is idle (suspended in `processEvents`), that drain happens in
  a microtask inside the same DOM dispatch, so `preventDefault()` from the
  C++ handler still lands in time and browser default actions are canceled.
- When the main thread is busy in wasm, or suspended outside
  `processEvents`, arriving events are queued with their dispatch already
  finished: `preventDefault()` comes far too late and **the browser executes
  the default action of every such event**.
- `ExcludeUserInputEvents` is a no-op on wasm (unimplemented in
  `QEventDispatcherWasm::sendNativeEvents`); input reaches widgets even
  inside a nested `exec`.

## Keyboard focus: how hotkeys die and how they heal

Qt receives keydown only while one of its DOM elements owns browser focus
(the per-window contenteditable `.qt-window-focus-helper` div; keydown
listeners sit on the window's client-area div). Two thieves:

- **Patchy-side DOM interactions.** Creating or clicking a DOM element (the
  picker's `<input>`, the download anchor) moves browser focus off Qt.
  Call `restore_qt_dom_focus()` (dialog_utils_wasm) after any such
  interaction.
- **Un-prevented press defaults.** A mouse/pen press dispatched during a
  busy stint (heavy composite, undo Document copy, layer-thumbnail refresh)
  is queued un-prevented, and Chrome's mousedown default blurs the focused
  element: browser focus falls to `<body>`, Qt gets no keydown at all, every
  hotkey dies while the mouse keeps working. Qt never notices, and clicking
  the canvas cannot recover: `QWasmWindow::requestActivateWindow()` skips
  DOM `focus()` whenever an input context exists (it always does), and
  `QWasmInputContext::updateInputElement()`, the only DOM-refocus path, runs
  only when the Qt focus object CHANGES - which a click on the
  already-focused canvas never does (clicking a focus-taking panel widget
  does, which is why users found "click the Layers panel" revives hotkeys).

Heals installed (all call `restore_qt_dom_focus()`, which refocuses Qt only
when focus actually fell to the page body/html, so it never steals a
widget's keyboard):

- `MainWindow::eventFilter` on every `MouseButtonPress`/`TabletPress`
  (application-level filter): by the time the queued press reaches C++, the
  theft already happened, so healing on the press itself makes any click
  self-repair.
- `CanvasWidget::wait_for_processing_operation` when the outermost wait
  unwinds, and `end_processing_operation` at depth zero: covers Tab-key
  navigation theft and presses swallowed by the wait guards.

## The pending-event queue crash and its guard

`QWasmSuspendResumeControl::sendPendingEvents()` (Qt 6.10) snapshots
`pendingEvents.length`, then `shift()`s that many times, invoking one C++
handler per event. A handler that nests an event loop - every slow-commit
processing wait does - re-enters `sendPendingEvents` and drains the same
queue. When the outer call resumes, its stale count `shift()`s an empty
queue and reads `["index"]` of `undefined`: the JS TypeError unwinds the
resumed Asyncify stack, the main exec loop is lost, and the tab parks dead
(mouse and keyboard both) with one console error, `Cannot read properties
of undefined (reading 'index')`. Trigger shape: input events queued behind
a press/release whose handler nests a wait, i.e. clicking or moving during
"Processing..." on a heavy document.

`install_wasm_pending_event_queue_guard()` (wasm_event_guard.cpp, installed
in the MainWindow constructor) registers a no-op handler with the control
(via the private QtCore header `qwasmsuspendresumecontrol_p.h`, pinned on
purpose) and patches the queue's `shift()` to return an
`{index: noop, arg: undefined}` sentinel instead of `undefined`, so
overshoot iterations dispatch to the no-op. Queue semantics are otherwise
untouched (`push` still sees the real array). Remove the guard and the
CorePrivate link when Qt re-reads the live length per iteration.

## Canvas guards during processing waits

**User input re-enters nested waits; the canvas guards it.** The wasm
plugin delivers DOM pointer and key input synchronously into a suspended
nested loop (see above). CanvasWidget's input handlers drop user input
while `processing_render_wait_active_`; mouse releases are parked and
replayed after the outermost wait unwinds (a dropped release would leave
the owning gesture latched), and ShortcutOverride is accepted so app-level
hotkeys cannot fire into a half-committed operation. Without this, the Move
release re-entered its own commit (mismatched deltas, ghost undo
snapshots). MainWindow's canvas event filter obeys the same rule: it leaves
`swallow_next_canvas_left_press_` untouched during a wait. The text
click-off commit runs INSIDE the press delivery (focus walk -> focus-loss
commit -> undo-snapshot wait), so the re-entrant release otherwise cleared
the flag before its press resumed and one click off opened a new text
session (`ui_text_click_off_commit_ignores_reentrant_release_during_wait`).
