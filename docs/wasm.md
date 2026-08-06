# WebAssembly (Emscripten) build

Deep reference for the wasm builds. Read this before touching the `wasm-core`
or `wasm-release` presets, the emsdk/Qt-kit provisioning, or `scripts/wasm/`.

## What exists today

Two wasm configurations share the pinned Emscripten 4.0.7 toolchain:

- **`wasm-core`**: the Qt-free engine libraries plus
  `patchy_core_tests`, run under node. No Qt (`PATCHY_BUILD_APP=OFF`).
- **`wasm-release`**: the full app linked against Qt for
  WebAssembly (6.10.3 `wasm_multithread`, static), running in a browser tab
  with Asyncify plus pthreads. File I/O, drag-in, and settings are
  browser-backed (details below). Background work runs on real threads; the
  deployment cost is cross-origin isolation (COOP/COEP headers, see
  deployment).

Desktop builds are unaffected; the presets, the `if(EMSCRIPTEN)` CMake
branches, the `Q_OS_WASM` gates in `src/ui`/`src/app`, and `scripts/wasm/`
are the whole wasm surface.

## Toolchain setup

```powershell
pwsh -File scripts\wasm\setup-emsdk.ps1
```

Idempotent: clones emsdk into `.deps\emsdk` (gitignored), `git pull`s an
existing clone (a stale checkout fails with "unknown version"), installs +
activates Emscripten 4.0.7, the Qt-supported version (`-EmsdkVersion`
provisions others; versions coexist). The bundled node 22.16.0 runs the
tests; the scripts glob `.deps\emsdk\node\*\bin\node.exe`, so keep exactly
one node directory there.

## Configure and build (wasm-core)

Same wrapper pattern as the Windows release preset: `scripts\vs-env.bat`
supplies cmake and ninja, `emsdk_env.bat` supplies emcc. Run from the repo
root in PowerShell or cmd, never a POSIX shell:

```powershell
cmd /s /c 'call .deps\emsdk\emsdk_env.bat >nul 2>&1 && call scripts\vs-env.bat -arch=x64 -host_arch=x64 >nul && "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --preset wasm-core'
```

Build with the same wrapper and `--build --preset wasm-core`. Output:
`build\wasm-core\patchy_core_tests.js` + `.wasm`. The zero-warning rule
applies; vendored suppressions stay scoped to one file and one diagnostic.

## Running the suite

```powershell
pwsh -File scripts\wasm\run-core-tests.ps1
```

Takes the usual name-substring filter as the first argument; runs the
emsdk-bundled node from `build\wasm-core`, so `test-artifacts/` lands there.
`ctest` also works there (the preset pins `CMAKE_CROSSCOMPILING_EMULATOR`).

The suite passes at the Windows count with the 2.4 GB
`local-test-fixtures/` corpus present, canaries byte-identical. Expected
`[SKIP]`s: one absent local fixture, two HEIC tests (node has no
`VideoDecoder`), and `af_modern_embeds_are_center_anchored_if_available`
(its fixture exceeds a 32-bit address space, the wasm32 cap). The engine
libraries carry no wasm `#ifdef`s; the one guard, in `tests/core/main.cpp`,
skips the crash-stack reporter (no `execinfo.h`; node prints trap stacks).

## wasm-core preset decisions (all in CMakePresets.json)

- `-fwasm-exceptions`: format readers throw `std::runtime_error` and the
  runner catches per test, so catching must be on. Native wasm EH, faster
  than JS-based `-fexceptions`.
- `-pthread`, `-sPTHREAD_POOL_SIZE=32`, `-sPTHREAD_POOL_SIZE_STRICT=2`: the
  engine's two `std::async` sites (`compositor.cpp` strip flatten,
  `psd_channel_data.cpp` CMYK conversion) spawn workers while the caller
  blocks, so the pool must pre-spawn the largest blocking fan-out; a lazily
  spawned worker deadlocks there. Both sites cap at 16 workers. STRICT=2
  makes pool exhaustion a hard error, not a silent hang.
- `-sNODERAWFS=1`: fixtures read from the real filesystem via
  `PATCHY_SOURCE_DIR`, `test-artifacts/` written to disk; the .js becomes
  node-only.
- Memory: growth to 4 GB, 256 MB initial, 8 MB stack (LibRaw's dcraw-derived
  decoders carry large stack locals; the 64 KB default is far too small),
  1 MB worker stacks.
- `-sENVIRONMENT=node,worker`, `-sEXIT_RUNTIME=1` (exit code from `main`;
  pool workers must not keep node alive), and `-Wno-pthreads-mem-growth`
  (informational; suppressed).
- `--pre-js scripts/wasm/node-env-pre.js` forwards `process.env` into
  Emscripten's synthetic environ before `main`, so
  `PATCHY_RENDER_SINGLE_THREADED` and the other env escape hatches work
  under node.

## The app build (wasm-release)

### Provisioning

```powershell
pwsh -File scripts\wasm\setup-emsdk.ps1
pwsh -File scripts\wasm\setup-qt-wasm.ps1
```

The second script installs Qt 6.10.3 `wasm_multithread` (plus qtimageformats)
into `.deps\Qt\6.10.3\wasm_multithread` via aqtinstall (venv
`.deps\aqt-venv`, upgraded each run), and the matching `win64_msvc2022_64`
host kit beside it. `QT_HOST_PATH` must be the same Qt version as the wasm
kit (moc, rcc, lrelease, and the staged `qtbase_ja.qm` come from it;
verified after install). `-WasmArch wasm_singlethread` and `-QtVersion`
select other kits; kits coexist under `.deps\Qt\<version>\`, so rollback is
a preset edit. Desktop presets stay on their vendored 6.8.3 kit. The preset
chains Qt's toolchain file into emsdk's via `QT_CHAINLOAD_TOOLCHAIN_FILE`.

Not 6.11 yet: released aqtinstall (3.3.0) cannot install a 6.11 desktop
host kit (per-arch folders, no base `Updates.xml`); revisit when aqtinstall
understands the layout.

Kit facts: 6.10 suspends via `EM_ASYNC_JS` (no `-sASYNCIFY_IMPORTS`);
Emscripten 3.1.58+ folds the pthread bootstrap into `patchy.js` (no
`patchy.worker.js`).

### Build, serve, stop

Configure and build like `wasm-core` but with `--preset wasm-release`.
Output: `build\wasm-release\patchy.html`, `patchy.js`, `patchy.wasm`,
`patchy.data` (preloaded fonts/translations/scripts), `qtloader.js`. Serve
locally:

```powershell
pwsh -File scripts\wasm\serve-app.ps1   # [port] [--open]; default port 8973
```

then open `http://localhost:8973/patchy.html`. `serve.mjs` sends the
COOP/COEP headers, so the threaded build works locally. The release wrappers
`scripts\release\start-local-wasm-test-server.bat` (raw build dir) and
`start-local-wasm-server.bat` (staged site) add port cleanup; see
[release-process.md](release-process.md).

When wasm work is finished, stop every local server you started with
`scripts\wasm\free-server-port.ps1 -Port <port>`, one call per port (repo
rule; see AGENTS.md). It stops a node listener on the port and leaves
non-node processes alone.

A hidden tab never fires requestAnimationFrame, so Qt stops presenting and
the tab looks frozen; nothing is wrong. Keep the tab foregrounded, or shim
requestAnimationFrame onto setTimeout before qtloader runs (harness below).

### Decisions and gates

- **HEIC uses the browser's HEVC decoder.** `src/formats/libheif` (app
  build only; `wasm-core` does not build it) statically links libheif
  1.23.1 as a container parser: `WITH_WEBCODECS=ON`, every software codec
  backend, encoder, plug-in loading, and the uncompressed codec disabled.
  The adapter feeds the `hvc1.*` configuration and access units to
  `VideoDecoder` in the file-open worker, calling `isConfigSupported()`
  first; no software fallback anywhere, so runtime capability is the only
  support promise (Safari 17.4+; elsewhere OS/device-dependent).
- **libheif license delivery.** LGPL-3.0-or-later source and license are
  vendored; Patchy's MIT source and reproducible wasm build instructions
  permit relinking with a modified library, and the staged site carries
  `NOTICE-THIRD-PARTY.md` plus `libheif-COPYING.txt`. Keep those files in
  `build-wasm.bat` and `upload-wasm-to-rtsoft.bat`.
- **Asyncify + JS exceptions.** `-sASYNCIFY` makes the nested-event-loop
  sites (`QDialog::exec`, `run_non_modal_dialog`, pumps) work unmodified.
  Asyncify does not support wasm-native exceptions, so the app compiles with
  JS-based `-fexceptions`; `wasm-core` keeps `-fwasm-exceptions`.
- **`-sASYNCIFY_STACK_SIZE=1048576` is load-bearing.** The 4 KB default
  overflows when a dialog suspends from another dialog's nested exec; in a
  non-assertion build that is an undiagnosed `RuntimeError: unreachable` tab
  crash. 1 MiB per live suspend is a few MB worst case.
- **JSPI (Asyncify's successor) dead-ends on the stock aqt kit**; revisit
  only with a source-built Qt (wasm EH plus `-feature-wasm_jspi`: measured
  42.2 MB against 65.6 MB with a far faster link; Chrome 137+, experimental
  on emsdk 4.0.7, a second artifact set). With JS `-fexceptions` the first
  idle suspend parks forever under invoke trampolines JSPI cannot suspend
  across; with `-fwasm-exceptions` Qt's prebuilt static libraries cannot
  link, and mixing modes is unsupported.
- **Codegen: compile `-msimd128`, link `-O3`, `-sMALLOC=mimalloc`**, adopted
  from interleaved in-app stress A/B runs (harness below): SIMD wins 5-16%
  on compute steps (canaries stay byte-identical), `-O3` beats `-Os` at
  runtime, and mimalloc fixes dlmalloc's single-lock contention under
  pthreads (5-8% whole-run).
- **QtQuick is excluded** (Qt6::Qml is linked for QJSEngine only):
  `QT_QML_MODULE_NO_IMPORT_SCAN TRUE` plus
  `qt_import_plugins(patchy EXCLUDE_BY_TYPE qmltooling)`, both required (the
  qmldbg default plugins pull the Quick scene graph back in). Together: -24%
  wasm size.
- **PATCHY_* escape hatches work in the browser.** `--pre-js
  scripts/wasm/app-env-pre.js` copies `PATCHY_`-prefixed keys from the page
  URL query string into the environment before `main`;
  `?PATCHY_RENDER_SINGLE_THREADED=1` is the in-build control group for
  threading comparisons. `PATCHY_WASM_INITIAL_MB`, `PATCHY_WASM_MAX_MB`, and
  `PATCHY_WASM_POOL` are consumed by the shell page itself before the Module
  exists (memory bullet below; the pool value lands in
  `globalThis.patchyPthreadPoolSize`, which the baked pool formula prefers.
  Perf-only: an undersized pool degrades blocking fan-outs to sequential, it
  cannot deadlock).
- **Compiled out or stubbed:** QtPrintSupport does not exist on wasm
  (`print_dialog_wasm.cpp` stubs; File menu hides Print/Page Setup; the
  portable half stays in `print_layout.cpp`). Single-instance QLocalServer
  off. Update check off (the site redeploy is the update mechanism; the
  GitHub fetch would fail CORS). Script sounds no-op. Scanner import off.
  Export Layers as Image Sequence hidden. Multi-file pickers degrade to one
  pick. Recents work within a session (MEMFS paths) and self-prune after
  reload. Script-editor plain Save downloads nothing (Save As does).
- **Assets.** `--preload-file` mounts staged copies at `/fonts`,
  `/translations`, `/scripts` inside `patchy.data`; `applicationDirPath()`
  is `/`, so existing directory probes work unchanged. `qtbase_ja.qm` is
  staged from the host kit (the wasm kit ships no `.qm`).
  `third_party/fonts-web` (~23 MB of OFL fonts, wasm only; see
  [fonts.md](fonts.md)) merges into the staged fonts; `LINK_DEPENDS` on the
  fonts stamp makes a fonts-only change repack `patchy.data`. The 8.7 MB
  texture pack stays embedded (lazy fetch is a future size lever).
- **Memory:** the shell page constructs the shared `WebAssembly.Memory` and
  passes it to qtLoad as `wasmMemory` (`buildWasmMemory`, patchy.html.in).
  `QT_WASM_INITIAL_MEMORY` (256 MB) is the FLOOR baked into the memory
  import: a smaller page-supplied initial is a LinkError, so the page's
  `BAKED_MIN_MB` must stay in sync (Qt's dev-loop patchy.html just uses the
  floor). The page picks initial 512 MB desktop / 256 MB iOS and walks a
  maximum ladder (4096/2048/1024 MB; iOS 1536/1024/768), catching the
  RangeError WebKit throws when it cannot reserve a shared maximum up front;
  iOS starts low because an oversized reservation can also succeed and get
  the tab killed later, uncatchably. `-sMAXIMUM_MEMORY=4GB` stays as the
  declared import ceiling. The chosen cap is published as
  `globalThis.patchyWasmMemoryMaximumBytes`, read by `ui/memory_info.hpp`
  for the About screen's live memory row (`emscripten_get_heap_max()` is
  baked at link time; never trust it for this). The About row shows three
  numbers: used (the allocator's live claim, `emmalloc_dynamic_heap_size()`
  minus `emmalloc_free_dynamic_memory()`; -sMALLOC=mimalloc layers mimalloc on
  emmalloc, and emmalloc's free-list bookkeeping is the coherent number where
  mimalloc's own mi_process_info stats wrap negative in the emscripten build),
  heap (`emscripten_get_heap_size()`, the linear-memory buffer browser tab
  accounting sees, which only ratchets), and the cap.
  `ui/wasm_memory_telemetry.cpp` (installed from the MainWindow constructor)
  can publish the same picture to `globalThis.patchyMemStats` every second
  (heapBytes, usedBytes, peakUsedBytes, limitBytes, historyBytes,
  historyBudgetBytes, seq, timestampMs; seq and timestampMs detect staleness
  during long synchronous compute) for page JS and the memory test harness.
  It is diagnostics OPT-IN and inert for release visitors: `?PATCHY_MEM_STATS=1`
  enables the publisher, `?PATCHY_MEM_LOG=1` additionally logs each sample to
  the console, and the harness page opts in automatically through
  `globalThis.patchyExtraEnv` (folded into the app environment by
  app-env-pre.js, explicit URL keys winning). Qt owns
  `-sSTACK_SIZE`; do not add a second one. In-app relief, because wasm memory never shrinks:
  history is
  byte-budgeted (256 MB on wasm, `history_memory_budget_bytes`, floor 3
  states/session) and the style caches shrink to 96/48 MB under `Q_OS_WASM`
  (image_document_io.cpp).

### Web file access

Real files never leave the browser sandbox; both directions stage through
MEMFS so the path-based pipeline runs unchanged. Opens copy picked or
dropped bytes to `/opened/<n>/<name>` (or `/dropped/<n>/<name>`) and hand
the path to `open_document_path`; saves let the existing writers write
`/saved/<n>/<name>`, then hand the bytes to the browser as a download. Glue:
`src/ui/dialog_utils_wasm.{hpp,cpp}`; `dialog_utils.cpp`'s three pickers
delegate there under `Q_OS_WASM`, and `offer_browser_download_for_saved_file`
(no-op on desktop) runs after each successful write in every save/export
path.

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
  rejects the `blob://placeholder` preview urls), so no double-open. The
  desktop `QDropEvent` path is untouched.
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
  presets last one session (persistence is a future candidate). Preset and
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
- **DOM interactions must give focus back.** Creating or clicking a DOM
  element (the picker's `<input>`, the download anchor) moves browser focus
  off Qt's screen element, and Qt then receives no keydown at all: every
  hotkey dies until the user clicks the canvas. Call
  `restore_qt_dom_focus()` (dialog_utils_wasm) after any such interaction;
  it refocuses Qt's window element only when focus was lost to the page
  body, so it never steals a widget's keyboard.

### Browser UI fit

- **Interface scale comes from the shell page, never QT_SCALE_FACTOR.** The
  wasm plugin takes pointer events from raw `offsetX`/`clientX` without
  applying Qt's high-DPI factor, so any factor but 1 renders scaled yet
  offsets every click by that factor. The shell reads
  `preferences/guiScalePercent` from localStorage (default 75%, synced
  with `kDefaultGuiScalePercent`) and at any other value re-embeds itself
  in a CSS-transformed iframe sized 10000/percent %, scaling
  `devicePixelRatio` inside for a 1:1 device backing store. Qt stays at
  factor 1; a change applies on reload.
- **Wheel events arrive as pixel deltas.** The wasm plugin fills both
  `pixelDelta` and `angleDelta` with browser pixels (~120 per notch), never
  the desktop 120-per-notch angle convention. Any custom wheel handler that
  feeds a per-item scrollbar must convert pixels through the row height
  (`LayerListWidget::scroll_by_wheel_delta`); applying the delta raw scrolls
  ~120 rows per notch. Stock Qt widgets are unaffected.
- **Float windows are disabled.** No window manager, no `startSystemMove`: a
  floated document covered the canvas with no way back.
  `MainWindow::float_document_session` no-ops on wasm and is the single
  funnel for every entry point. The window-arrangement actions stay
  registered but hidden (hotkey-id stability, like Print); the tab tear-off
  gesture is compiled out. See [float-windows.md](float-windows.md).
- **Non-modal dialogs are restacked above their parent.** The wasm
  compositor raises a clicked window without its transient children.
  `keep_dialog_above_parent_window` (dialog_utils.cpp) registers every
  dialog `run_non_modal_dialog` shows and restacks a window's registered
  dialogs above it one event-loop turn after a press or activation reaches
  it, walking the parent chain. Do not bring back
  `Qt::WindowStaysOnBottomHint` on the main window: 6.10 honors transient
  parents, and the hint sent main-window-parented dialogs to the bottom
  zone, invisible.
- **Modal dialogs are raised when they block.** The compositor inserts a
  modal directly above its transient parent (usually the bottom-most main
  window), so a modal opened under a higher non-modal dialog sits beneath
  it, invisible and swallowing every click. `WasmDialogRaiser`
  watches `QEvent::WindowBlocked` and raises + activates
  `QApplication::activeModalWidget()` one turn later, covering Qt's static
  dialogs too. `exec_dialog` installs the guard before its exec.
- **Dialogs clamp to the canvas.** On wasm `place_dialog` (dialog_utils.cpp,
  the funnel under `exec_dialog`/`run_non_modal_dialog`) shrinks a dialog,
  and any explicit minimum, to the available screen (no window manager can
  rescue an off-screen button row). When even the layout minimum exceeds the
  canvas, `install_dialog_overflow_scroll` moves the content into a
  `QScrollArea` (`dialogOverflowScroll`); dark-chrome dialogs keep the title
  bar fixed. The clamp runs at placement time only. This is also why dialogs
  are shown through `exec_dialog`/`run_non_modal_dialog`, never a bare
  `QDialog::exec`.
- **Right-dock panel toggles repaint the whole window.** The wasm backing
  store keeps stale pixels where a dock relayout moved content, so
  `MainWindow::handle_right_dock_panel_toggled` calls `update()` after the
  deferred relayout; the dock width handle relies on Qt's implicit
  press-grab (explicit `grabMouse` is unreliable here).
- **A window shown under an application-modal window never gets its
  keyboard back.** Qt marks windows created while an app-modal window is
  visible as blocked, the key path drops events for blocked windows (the
  mouse path does not), and the wasm plugin never clears the flag when the
  modal hides: the window paints and takes clicks but never receives a
  keystroke again. Script canvas windows hit this when the app-modal script
  stop panel was up at creation time. Guards (script host): `createCanvas`
  calls `dismiss_busy_indicator()` before creating the window;
  `pump_progress_indicator` will not raise the stop panel while the run owns
  an open canvas window; interactive helpers pause via `ModalWatchdogPause`.
  Tests: `ui_script_canvas_window_dismisses_stop_panel`,
  `ui_script_canvas_window_suppresses_stop_panel`. Rule: never create a
  window while the app-modal busy panel can be up; dismiss it first.

### Threads and blocking

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
  (fine from worker threads) instead of aborting a visitor's session. Worker
  stacks are 4 MB; LibRaw decode and full compositor walks run there.
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
  awaited compute never needs a lazy spawn mid-wait (the free-transform
  release's chained worker fan-outs otherwise park the tab).
- Main-thread waits suspend in an event loop.
  `wait_for_processing_operation` (canvas_widget_render.cpp) waits in a
  nested QEventLoop woken by a 100 ms poll QTimer on threaded wasm, so
  workers return to the pool, lazy spawns complete, and the processing
  overlay paints. Not 16 ms: `operation_ready` itself blocks up to 16 ms, so
  a 16 ms interval starved the loop. `run_filter_compute_with_progress`
  (main_window_shared) is the same shape for all five commit sites
  (destructive filter, Filter Gallery, Liquify, Levels, Curves); the
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

**User input re-enters nested waits; the canvas guards it.** The wasm
plugin delivers DOM pointer and key input synchronously into a suspended
nested loop, so `ExcludeUserInputEvents` is no defense there (it only
defers queued window-system events). CanvasWidget's input handlers drop
user input while `processing_render_wait_active_`; mouse releases are
parked and replayed after the outermost wait unwinds (a dropped release
would leave the owning gesture latched), and ShortcutOverride is accepted
so app-level hotkeys cannot fire into a half-committed operation. Without
this, the Move release re-entered its own commit (mismatched deltas, ghost
undo snapshots).
MainWindow's canvas event filter obeys the same rule: it leaves
`swallow_next_canvas_left_press_` untouched during a wait. The text
click-off commit runs INSIDE the press delivery (focus walk -> focus-loss
commit -> undo-snapshot wait), so the re-entrant release otherwise cleared
the flag before its press resumed and one click off opened a new text
session (`ui_text_click_off_commit_ignores_reentrant_release_during_wait`).

**No `processEvents` pump runs mid-operation on wasm.** The browser gets no
paint or input turn until the main thread suspends in an idle event loop; a
pump only dispatches Qt timers into the running operation, which once froze
a tab past recovery on a slow script. Three pumps are compiled out under
`Q_OS_WASM`: the script busy-indicator pump (`pump_progress_indicator`,
script_engine.cpp), the processing-overlay tick, and the overlay-show pump
(`show_processing_overlay`), both canvas_widget_render.cpp. Long
synchronous bursts show no progress until they yield; long filter work
already runs on a worker. Companion guards: `call_script_callback` refuses
reentry while script code is executing; the script canvas frame timer is
single-shot, re-armed per frame.

Slow live previews paint a "Rendering preview..." canvas badge on every
platform (see [filters.md](filters.md)); it matters most on wasm.

Single-threaded builds: `should_defer_full_refresh_to_async` /
`should_defer_first_render_to_async` (canvas_widget_render.cpp) return false
when `kBackgroundWorkRunsInline` (background_workers.hpp): deferring to an
inline worker composed the frame inside paintEvent anyway.
`build\wasm-st-baseline` preserves a single-threaded build for comparisons.

## Known issue: Safari 26 kills the tab within minutes (August 2026)

Measured on studiomac (macOS 26.3.1, Safari 26.x) with the memtest harness
(see [performance.md](performance.md)): the app's WebContent process grows
about 150 MB/s at IDLE with 400-1200% CPU and is killed by WebKit at roughly
2.5 minutes (footprint plateaued at 16 GB, ps rss reached 24 GB). The wasm
side is innocent: patchyMemStats stays flat (512 MB heap, ~100 MB used), and
the `footprint` category breakdown puts the growth in "WebKit malloc" (2.7 GB
dirty 6 seconds after load), not the JS GC heap or JIT-code regions. Chrome on
the same machine with the same page holds flat at ~900 MB. The signature
(concurrent compile threads burning CPU while allocating unboundedly, other
browsers unaffected) matches public Safari/WebKit 26 reports against large
wasm modules, e.g. onnxruntime issue 26827, where sampling showed
JSC::Wasm::parseAndCompileOMG looping in allocateStackByGraphColoring.
A launchctl-env JSC_useOMGJIT=false test did not change the behavior, but env
propagation into WebContent XPC was unverified, so tier attribution is open.
iOS Safari deaths ~2 s after load are consistent with the same compile-side
growth against a phone's jetsam budget and would be knob-independent (the
memory-ladder and pool URL knobs cannot dodge it). Leads: shrink/split the
66 MB module or the pathological function(s) that blow up JSC's compiler, and
file a WebKit bug (rtsoft.com/patchy is a clean public repro).

## Release deployment (rtsoft.com/patchy)

Batch-file details live in [release-process.md](release-process.md):
`build-wasm.bat` stages `build\package\wasm-site`,
`upload-wasm-to-rtsoft.bat` publishes to `rtsoft.com/patchy`; local-server
wrappers above.

**Cross-origin isolation is a hard serving requirement.** SharedArrayBuffer
needs `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`. Three layers: the staged
`.htaccess` sets both (IfModule-guarded), `serve.mjs` sends them locally,
and the shell page checks `window.crossOriginIsolated` before fetching the
wasm and names the two headers in its error; the post-upload curl check
that catches a host silently dropping them is in
[release-process.md](release-process.md). Every asset is same-origin, so no
per-asset CORP headers are needed.

The deployed page is a Patchy-branded shell from
`packaging\web\patchy.html.in`, not Qt's generated `patchy.html` (the
dev-loop page; staging, cache tags, and the compressed `.br`/`.gz` serving
negotiation are in [release-process.md](release-process.md)). Emscripten
has no download-progress callback, so the page fetches `patchy.wasm` itself
with a byte-counting reader into one preallocated exact-size buffer,
compiles it with `WebAssembly.compile`, and passes the module to `qtLoad`
as `qt.module`, so the fetched bytes are collectable after compile
(`wasmBinary` would be glue-retained for the session, ~66 MB). The page
also constructs the memory (bullet above), appends a plain-language hint to
the crash screen when the abort text looks like out-of-memory, and versions
the `patchy.data` fetch via `locateFile`. No special MIME is needed (the
page compiles from bytes; streaming instantiation is unused).

## Headless stress harness

The wasm stress/A-B harness (hidden-tab timer shims, interleaved two-port
comparisons, `--run-script` mode) is documented in
[performance.md](performance.md).

## Later steps (not built yet)

Remaining: texture lazy-fetch, the advertised document-size cap,
and preset/library persistence across reloads (follow the poll-pattern
IndexedDB glue user fonts already use in user_fonts_wasm.cpp, not IDBFS).
History byte budget and wasm cache caps shipped (memory bullet above);
tile-cache eviction is moot while `src/render/tile_cache.hpp` stays dead
code.
