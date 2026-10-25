# WebAssembly: web file access, threads and blocking

Split from [wasm.md](wasm.md) (presets, provisioning, decisions and gates, browser UI fit, release). Memory diagnostics: [wasm-memory.md](wasm-memory.md); input and focus: [wasm-input.md](wasm-input.md).

## Web file access

Real files never leave the browser sandbox; both directions stage through
MEMFS so the path-based pipeline runs unchanged. Opens stream picked or
dropped bytes directly into an exactly-sized `/opened/<n>/<name>` (or
`/dropped/<n>/<name>`) MEMFS file and hand the path to `open_document_path`;
saves let the existing writers write
`/saved/<n>/<name>`, then hand the bytes to the browser as a download. Glue:
`src/ui/dialog_utils_wasm.{hpp,cpp}`; `dialog_utils.cpp`'s three pickers
delegate there under `Q_OS_WASM`, and `offer_browser_download_for_saved_file`
(no-op on desktop) runs after each successful write in every save/export
path.

The page uses `Blob.stream()` after exact MEMFS preallocation
(`arrayBuffer()` is an old-engine fallback), so no full source-file
`QByteArray` enters the wasm heap; multi-file drops stage sequentially. After
load, `/opened` and `/dropped` sources are removed and sessions stay pathless,
so Save uses Save As and Reopen/Reveal never target dead sandbox paths. The
drop drain also removes rejected or modal-blocked inputs.

Three platform findings constrain the shape; do not regress them:

- **A JS promise cannot complete into a nested event loop.** While the app
  is suspended in a nested `QEventLoop::exec` (Asyncify), DOM events still
  re-enter through Qt's event queue but qstdweb promise callbacks never
  arrive, so `QFileDialog::getOpenFileContent` deadlocks if anything blocks
  on it. The picker uses its own `<input type=file>` whose change/cancel
  handlers are pure page-side JS writing a plain JS object, polled from C++
  by a QTimer (timers reliably resume the suspended loop). Never wait on a
  Qt async JS API from a nested loop.
- **External drops arrive through page-side glue, drained by a Qt timer.**
  `install_web_drop_target` (MainWindow constructor) installs page-side
  dragover/drop listeners, reads dropped files into a plain JS queue, and a
  250 ms QTimer drains it into `MainWindow::handle_web_file_drop`, one MEMFS
  path at a time. Qt 6.10's own drop listeners are inert for external drops
  (deferred handlers see a neutered `dataTransfer`; `accept_open_file_drag`
  rejects the `blob://placeholder` preview urls), so no double-open.
- **A raw JS-to-wasm export call must never lead to a nested event loop.**
  JS (promise callbacks, hand-registered listeners) must not call a wasm
  export whose C++ path can suspend: that entry runs outside Qt's
  suspend-resume control, and if the idle-suspended main loop already has a
  resume in flight, the nested suspend clobbers the single-slot Asyncify
  state; the app parks forever with a clean console. Page-side
  JS writes plain state; C++ polls it from a QTimer, whose handler runs
  inside the resumed main loop, where nesting an exec is safe.

Other step-3 decisions:

- **Save As and Export prompt for a name and format** in a small dialog
  (`wasmSaveFileDialog`); the chosen filter row flows back through
  `selected_filter`, so `path_with_default_extension` and every caller work
  unchanged. The session is marked clean after the MEMFS write.
- **Downloads use our own Blob + `<a download>` anchor click**, not
  `QFileDialog::saveFileContent`: Chrome's save picker needs transient user
  activation (a long PSD write outlives it) and can cancel with no signal
  after the session was already marked saved.
- **Settings persist in localStorage.** `app_settings()` uses
  `QSettings::WebLocalStorageFormat` on wasm (synchronous backend);
  `IniFormat` would land in MEMFS and evaporate on reload. Keys look like
  `qt-v0-Patchy-Patchy-<key>`.
- **Preset libraries re-seed each session.** Library files live under
  `/presets/<subdir>` in MEMFS and vanish on reload while the seeding stamps
  persist, so `stored_default_asset_version` (main_window_tool_options.cpp)
  treats every wasm session as unseeded. Defaults return each reload; user
  presets last one session. Preset and
  palette import/export goes through the shared `get_open_file_name` /
  `get_save_file_name` wrappers plus `offer_browser_download_for_saved_file`;
  only the scripting `getExistingDirectory` pickers still browse MEMFS (a
  browser cannot pick a host directory).
- **User-added fonts persist in IndexedDB** (DB `PatchyUserFonts`,
  `src/ui/user_fonts_wasm.cpp`): dropped fonts or zips register immediately,
  and a startup QTimer polls the page-side read to re-register them each
  boot. Same poll pattern: the page-side callbacks only write plain JS state
  or the database, never a wasm export. The IndexedDB put must copy bytes
  out of the heap first, both for heap growth and because the multithreaded
  heap is a SharedArrayBuffer, whose views structured clone refuses. See
  [fonts.md](fonts.md).
- **One picker at a time:** a second Open while the chooser is up would nest
  a second Asyncify suspend and hang the runtime; the wait dialog is modal
  and `pick_open_file` carries a re-entrancy guard.
- **Keyboard focus is fragile.** Qt receives keydown only while its
  focus-helper element owns browser focus; DOM interactions and busy-stint
  presses both steal it. Thieves, heals, and recovery rules:
  [wasm-input.md](wasm-input.md). Call `restore_qt_dom_focus()` after any
  code that creates or clicks a DOM element.

## Threads and blocking

The kit is `wasm_multithread`: pthreads on a SharedArrayBuffer heap (hence
the COOP/COEP serving rule). Threading seams in
`ui/background_workers.{hpp,cpp}` and the script watchdog in
`ui/script_engine.cpp` are gated on
`defined(Q_OS_WASM) && !defined(__EMSCRIPTEN_PTHREADS__)`: this build takes
the real-thread branches, a single-threaded wasm build runs work inline, and
the strip renderers just read `hardware_concurrency()`. Never swap the
ready-future sites to `std::launch::deferred`: the `wait_for == ready` event
pumps would spin forever.

Two Emscripten facts shape every rule here:

1. A pthread only starts on a pre-spawned pool worker while its spawner
   blocks; a lazily spawned Worker needs the spawning thread back in the JS
   event loop first.
2. A finished pthread's worker only returns to `PThread.unusedWorkers` when
   the main thread's event loop runs its `cleanupThread` message (a
   main-thread futex wait services sync-proxied `pthread_create`, not plain
   `worker.onmessage`), so back-to-back fan-outs inside one compute
   permanently consume workers while the main thread is blocked.

Consequences (do not regress):

- Pool size: `QT_WASM_PTHREAD_POOL_SIZE` pre-spawns
  `min(hardwareConcurrency,16)+16` workers (page-overridable; see
  `PATCHY_WASM_POOL` above). The CMYK site and both strip renderers cap at
  16 workers. `PTHREAD_POOL_SIZE_STRICT` stays unset: overflow lazily spawns
  (fine from worker threads) instead of aborting the session. Worker stacks
  are 4 MB (LibRaw decode and full compositor walks run there).
- Headroom alone is insufficient once busy workers shrink the idle pool
  below a blocking join's fan-out. `max_blocking_fanout_workers`
  (core/worker_budget.{hpp,cpp}) clamps every main-thread blocking fan-out
  to the idle pre-spawned pool (`PThread.unusedWorkers.length` minus a race
  margin of 2, via EM_ASM) and falls back to the sequential path below two
  free. Strip count never changes output bytes, so the canaries are
  unaffected.
- Worker-side fan-outs under an awaited compute are budgeted too: before
  `launch_async` the main thread publishes `idle - 3` in a
  `BlockingFanoutBudgetScope` (core/worker_budget), so
  `max_blocking_fanout_workers` also clamps worker-thread callers and the
  awaited compute never needs a lazy spawn mid-wait (chained worker
  fan-outs such as the free-transform release otherwise park the tab).
- Main-thread waits suspend in an event loop.
  `wait_for_processing_operation` (canvas_widget_render.cpp) waits in a
  nested QEventLoop woken by a 100 ms poll QTimer on threaded wasm, so
  workers return to the pool, lazy spawns complete, and the processing
  overlay paints. Not 16 ms: `operation_ready` itself blocks up to 16 ms, so
  a 16 ms interval starves the loop. `run_filter_compute_with_progress`
  (main_window_shared) is the same shape for the destructive filter, Filter
  Gallery, Liquify, Levels, Curves, and Divide Scanned Photos computes; the
  progress dialog paints and can cancel. Desktop keeps the on-thread
  compute.
- Callers reachable from paintEvent, or running when the pool is dry,
  compute inline instead (`render_document_image_with_processing`,
  `render_document_patches_with_processing`, the transform release refresh,
  `push_undo_snapshot`): a nested exec inside paintEvent is not safe, and
  inline cannot wedge because main-thread fan-outs are pool-clamped.

The rule for new code: never block the wasm main thread outside an event
loop while a worker it waits on may itself create threads, and never assume
a blocking fan-out can reuse workers freed by an earlier fan-out in the same
blocked stretch.

**Input delivery, the canvas wait guards, stolen keyboard focus, and the
Qt 6.10 pending-event queue crash (plus its `wasm_event_guard.cpp` shim)
live in [wasm-input.md](wasm-input.md).** Read it before touching input,
hotkeys, focus, or the waits: user input re-enters nested waits
synchronously (`ExcludeUserInputEvents` is a no-op on wasm), and a handler
that nests an event loop must never assume the pending-event queue still
holds the events its caller counted.

**No `processEvents` pump runs mid-operation on wasm.** The browser gets no
paint or input turn until the main thread suspends in an idle event loop; a
pump only dispatches Qt timers into the running operation and can freeze a
tab past recovery. Three pumps are compiled out under
`Q_OS_WASM`: the script busy-indicator pump (`pump_progress_indicator`,
script_engine.cpp), the processing-overlay tick, and the overlay-show pump
(`show_processing_overlay`), both canvas_widget_render.cpp. Long
synchronous bursts show no progress until they yield; long filter work
and the Remove Object fill run on a worker instead. Companion guards: `call_script_callback` refuses
reentry while script code is executing; the script canvas frame timer is
single-shot, re-armed per frame.

Slow live previews paint a "Rendering preview..." canvas badge on every
platform (see [filters.md](filters.md)); it matters most on wasm.

Single-threaded builds: `should_defer_full_refresh_to_async` /
`should_defer_first_render_to_async` (canvas_widget_render.cpp) return false
when `kBackgroundWorkRunsInline` (background_workers.hpp), since an inline
worker would compose the frame inside paintEvent anyway.
