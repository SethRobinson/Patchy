# WebAssembly (Emscripten) build

Deep reference for the wasm builds. Read this before touching the `wasm-core`
or `wasm-release` presets, the emsdk/Qt-kit provisioning, or anything under
`scripts/wasm/`.

## What exists today

Two wasm configurations share the pinned Emscripten 4.0.7 toolchain:

- **`wasm-core`** (step 1): the Qt-free engine libraries plus
  `patchy_core_tests`, run under node. No Qt (`PATCHY_BUILD_APP=OFF`).
- **`wasm-release`** (steps 2-4a): the full app linked against Qt for
  WebAssembly (6.10.3 `wasm_multithread`, static), running in a browser tab
  with Asyncify plus pthreads. File open/save/export go through the browser
  (picker in, downloads out), desktop drag-in works, settings persist in
  localStorage. Background work runs on real threads; the deployment cost is
  cross-origin isolation (COOP/COEP headers, see deployment).

Desktop builds are unaffected; the presets, the `if(EMSCRIPTEN)` CMake
branches, the `Q_OS_WASM` gates in `src/ui`/`src/app`, and `scripts/wasm/`
are the whole wasm surface.

## Toolchain setup

```powershell
pwsh -File scripts\wasm\setup-emsdk.ps1
```

Idempotent. Clones emsdk into `.deps\emsdk` (gitignored), refreshes an
existing clone with `git pull` (a stale checkout fails with "unknown
version"), installs + activates Emscripten 4.0.7 (the version the Qt
6.10/6.11 docs list as supported). `-EmsdkVersion` provisions another
release; versions coexist. Activation writes only emsdk's local config. The
bundled node 22.16.0 runs the tests; the scripts glob
`.deps\emsdk\node\*\bin\node.exe`, so keep exactly one node directory there.

## Configure and build (wasm-core)

Same wrapper pattern as the Windows release preset: `scripts\vs-env.bat`
supplies cmake and ninja, `emsdk_env.bat` supplies emcc. Run from the repo
root in PowerShell or cmd, never a POSIX shell:

```powershell
cmd /s /c 'call .deps\emsdk\emsdk_env.bat >nul 2>&1 && call scripts\vs-env.bat -arch=x64 -host_arch=x64 >nul && "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --preset wasm-core'
```

Build with the same wrapper and `--build --preset wasm-core`. Output:
`build\wasm-core\patchy_core_tests.js` + `.wasm`. The zero-warning rule
applies; vendored suppressions stay scoped to one file and one diagnostic
(so far only miniz's `-Wno-#pragma-messages` in the root CMakeLists).

## Running the suite

```powershell
pwsh -File scripts\wasm\run-core-tests.ps1
```

Takes the usual name-substring filter as the first argument; runs the
emsdk-bundled node from `build\wasm-core`, so `test-artifacts/` lands there.
`ctest` also works there (the preset pins `CMAKE_CROSSCOMPILING_EMULATOR`).

Status: 747 pass, 0 fail with the 2.4 GB `local-test-fixtures/` corpus
present (0.87, August 2026), matching the Windows release suite count; the
three byte-stability canaries pass byte-identically. Expected
`[SKIP]`s: one
absent local fixture, two HEIC tests (node has no `VideoDecoder`), and
`af_modern_embeds_are_center_anchored_if_available` (its fixture exceeds a
32-bit address space; that wasm32 ceiling becomes an advertised cap in
step 4). Zero wasm `#ifdef`s in the engine libraries; the one guard, in
`tests/core/main.cpp`, skips the crash-stack reporter (no `execinfo.h`; node
prints trap stacks itself).

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
- Memory: `-sALLOW_MEMORY_GROWTH=1` up to `-sMAXIMUM_MEMORY=4GB`,
  `-sINITIAL_MEMORY=256MB`, `-sSTACK_SIZE=8MB` (LibRaw's dcraw-derived
  decoders carry large stack locals; the 64 KB default is far too small),
  1 MB worker stacks.
- `-sENVIRONMENT=node,worker`, `-sEXIT_RUNTIME=1` (exit code from `main`;
  pool workers must not keep node alive), and `-Wno-pthreads-mem-growth`
  (informational, does not apply; suppressed to stay zero-warning).
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

Not 6.11 yet: released aqtinstall (3.3.0) cannot install a 6.11 desktop host
kit (download.qt.io moved 6.11 desktop into per-arch folders with no base
`Updates.xml`). Revisit when aqtinstall understands the layout.

Kit history: the upgrade from 6.8.3 / emsdk 3.1.56 fixed nested-exec
windows taking no input and non-modal dialogs opening invisibly behind
their parent (QTBUG-145018/102827/131699), deleting a large workaround
layer. Regression symptom for nested-loop input: an embind listener
registered as a Promise where a listener object belongs. 6.10 suspends via
`EM_ASYNC_JS` (no `-sASYNCIFY_IMPORTS`); Emscripten 3.1.58+ folds the
pthread bootstrap into `patchy.js` (no `patchy.worker.js`).

### Build, serve, stop

Configure and build like `wasm-core` but with `--preset wasm-release`.
Output: `build\wasm-release\patchy.html`, `patchy.js`, `patchy.wasm`,
`patchy.data` (preloaded fonts/translations/scripts), `qtloader.js`. Serve
locally:

```powershell
pwsh -File scripts\wasm\serve-app.ps1   # [port] [--open]; default port 8973
```

then open `http://localhost:8973/patchy.html`. Its `serve.mjs` sends the
COOP/COEP headers, so the threaded build works locally. The release wrappers
`scripts\release\start-local-wasm-test-server.bat` (raw build dir) and
`start-local-wasm-server.bat` (staged site) add port cleanup; see
[release-process.md](release-process.md).

When wasm work is finished, stop every local server you started, one call
per port used:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\wasm\free-server-port.ps1 -Port 8973
```

It stops a node listener left on the port (waiting for the socket to clear)
but leaves any non-node process alone.

A hidden tab never fires requestAnimationFrame, so Qt stops presenting and
the tab looks frozen; nothing is wrong. Keep the tab foregrounded, or shim
requestAnimationFrame onto setTimeout before qtloader runs (harness below).

### Decisions and gates (step 2)

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
- **`-sASYNCIFY_STACK_SIZE=1048576` is load-bearing.** The default 4 KB
  buffer holds the unwound stack per suspend; a dialog opened from another
  dialog's nested exec overflows it, and in a non-assertion build overflow
  is an undiagnosed `RuntimeError: unreachable` tab crash. 1 MiB per live
  suspend is a few MB worst case.
- **JSPI (Asyncify's successor) dead-ends on the stock aqt kit**; revisit
  only with a source-built Qt. With JS `-fexceptions` the first Qt idle
  suspend sits under exception-handling invoke trampolines JSPI cannot
  suspend across (permanent silent park); with `-fwasm-exceptions` Qt's
  prebuilt static libraries (Emscripten-JS setjmp, `emscripten_longjmp`)
  cannot link, and mixing modes is unsupported. A source-built kit (wasm EH
  plus `-feature-wasm_jspi`) measured 42.2 MB against 65.6 MB and a far
  faster link; needs Chrome 137+, still experimental on emsdk 4.0.7, and
  would ship as a second artifact set.
- **Codegen: compile `-msimd128`, link `-O3`, `-sMALLOC=mimalloc`**, all
  adopted from interleaved in-app stress A/B runs (harness below): SIMD wins
  5-16% on compute steps (canaries stay byte-identical), `-O3` beats `-Os`
  at runtime (that earlier choice measured size only; user speed outranks
  size here), and mimalloc fixes dlmalloc's single-lock contention under
  pthreads (5-8% whole-run).
- **QtQuick is excluded.** Qt's static-build finalizer would link the dead
  QtQuick stack (Qt6::Qml is linked for QJSEngine only). Two guards, both
  required: `QT_QML_MODULE_NO_IMPORT_SCAN TRUE` on the patchy target (stops
  qmlimportscanner walking `.deps`) and
  `qt_import_plugins(patchy EXCLUDE_BY_TYPE qmltooling)` (the qmldbg default
  plugins pull the Quick scene graph back in). Together: -24% wasm size.
- **PATCHY_* escape hatches work in the browser.** `--pre-js
  scripts/wasm/app-env-pre.js` copies `PATCHY_`-prefixed keys from the page
  URL query string into the environment before `main`;
  `?PATCHY_RENDER_SINGLE_THREADED=1` is the in-build control group for
  threading comparisons.
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
  texture pack stays embedded (lazy fetch is a step-4 size lever).
- **Memory:** `QT_WASM_INITIAL_MEMORY` is 512 MB with growth to a 4 GB cap.
  Qt's target machinery owns `-sSTACK_SIZE`; do not add a second one.

### Web file access (step 3)

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
  state; the app parks forever with a clean console (the drop glue once did
  this from its Promise.all callback and froze the deployed site). Page-side
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
  presets last one session (persistence is a step-4 candidate; the preset
  managers' import/export buttons browse MEMFS only).
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

### Browser UI fit

- **Wheel events arrive as pixel deltas.** The wasm plugin fills both
  `pixelDelta` and `angleDelta` with browser pixels (~120 per notch), never
  the desktop 120-per-notch angle convention. Any custom wheel handler that
  feeds a per-item scrollbar must convert pixels through the row height
  (`LayerListWidget::scroll_by_wheel_delta`); applying the delta raw scrolls
  ~120 rows per notch. Stock Qt widgets are unaffected.
- **Start panel web note** (`startPanelWasmNote`, start_panel.cpp): runs
  locally, fonts can be dropped in, desktop download link.
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
  window), so a modal opened under a higher non-modal dialog appeared
  underneath it, invisible and swallowing every click. `WasmDialogRaiser`
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

### Threads and blocking (step 4a)

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

- Pool size: the app sets `QT_WASM_PTHREAD_POOL_SIZE` to
  `Math.min(navigator.hardwareConcurrency,16)+16`. The CMYK site and both
  strip renderers cap at 16 workers. `PTHREAD_POOL_SIZE_STRICT` stays unset:
  overflow lazily spawns (fine from worker threads) instead of aborting a
  visitor's session. Worker stacks are 4 MB; LibRaw decode and full
  compositor walks run there.
- Headroom alone is insufficient: a rotate-then-merge on a 60 MB PSD wedged
  the deployed tab once busy preview/thumbnail/undo workers left the idle
  pool smaller than the merge's 16-strip main-thread join.
  `max_blocking_fanout_workers` (core/worker_budget.{hpp,cpp}) clamps every
  main-thread blocking fan-out to the idle pre-spawned pool
  (`PThread.unusedWorkers.length` minus a race margin of 2, via EM_ASM) and
  falls back to the sequential path below two free. Strip count never
  changes output bytes, so the canaries are unaffected.
- Worker-side fan-outs under an awaited compute are budgeted too: before
  `launch_async` the main thread publishes `idle - 3` in a
  `BlockingFanoutBudgetScope` (core/worker_budget), so
  `max_blocking_fanout_workers` also clamps worker-thread callers and the
  awaited compute never needs a lazy spawn mid-wait. Without it, the
  free-transform release (resample fan-out, then patch-render fan-out, on a
  worker) deterministically overran the pool and parked the tab.
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

**No `processEvents` pump runs mid-operation on wasm.** The browser gets no
paint or input turn until the main thread suspends in an idle event loop; a
pump only dispatches Qt timers into the running operation, which once froze
a tab past recovery on a slow script. Both pumps are compiled out under
`Q_OS_WASM`: the script busy-indicator pump (`pump_progress_indicator`,
script_engine.cpp) and the processing-overlay tick
(canvas_widget_render.cpp). Long synchronous bursts show no progress until
they yield; long filter work already runs on a worker. Companion guards:
`call_script_callback` refuses reentry while script code is executing; the
script canvas frame timer is single-shot, re-armed per frame.

Slow live previews paint a "Rendering preview..." canvas badge on every
platform (see [filters.md](filters.md)); it matters most on wasm.

Single-threaded builds: `should_defer_full_refresh_to_async` /
`should_defer_first_render_to_async` (canvas_widget_render.cpp) return false
when `kBackgroundWorkRunsInline` (background_workers.hpp): deferring to an
inline worker composed the frame inside paintEvent anyway.
(`build\wasm-st-baseline` preserves a single-threaded build for
comparisons.)

## Release deployment (rtsoft.com/patchy)

Batch-file details live in [release-process.md](release-process.md).
`scripts\release\build-wasm.bat` builds `wasm-release` and stages the
deployable files into `build\package\wasm-site`;
`upload-wasm-to-rtsoft.bat` publishes to `rtsoft.com/patchy` over ssh/scp;
local-server wrappers above.

**Cross-origin isolation is a hard serving requirement.** SharedArrayBuffer
needs `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`. Three layers: the staged
`.htaccess` sets both (IfModule-guarded), `serve.mjs` sends them locally,
and the shell page checks `window.crossOriginIsolated` before fetching the
wasm and names the two headers in its error. IfModule would silently drop
them on a host without `mod_headers`, so `upload-wasm-to-rtsoft.bat` curls
the live site after every upload and fails loudly if either is missing.
Every asset is same-origin, so no per-asset CORP headers are needed.

The deployed page is a Patchy-branded shell from
`packaging\web\patchy.html.in` (logo, version, favicon, real progress bar),
not Qt's generated `patchy.html` (the dev-loop page). Emscripten has no
download-progress callback, so the page fetches `patchy.wasm` itself with a
byte-counting stream reader and hands the bytes to `qtLoad` as `wasmBinary`;
its `locateFile` override versions the `patchy.data` fetch. `build-wasm.bat`
stamps a per-build cache tag into every asset reference (`?v=<tag>`); the
staged `.htaccess` marks html `no-cache` and tagged assets cache-forever,
all IfModule-guarded, so a redeploy shows on the next load. No special MIME
is needed (with `wasmBinary` supplied, streaming instantiation is unused).

Compressed serving: after staging, `build-wasm.bat` runs
`scripts\wasm\precompress-site.mjs` (emsdk node, zlib built-ins), writing
`.br` and `.gz` variants beside `patchy.wasm`, `patchy.js`, `patchy.data`,
and `qtloader.js`; first-visit transfer drops from ~92 MB to ~29 MB.
`.htaccess` serves them via `AddEncoding` plus `RemoveType` and
IfModule-guarded rewrite rules keyed on Accept-Encoding and file existence
(identity fallback without the modules). `serve.mjs` mirrors the negotiation
locally. The progress bar counts decompressed bytes, so `build-wasm.bat`
bakes the uncompressed wasm size into the page (`__PATCHY_WASM_SIZE__`) as
the total; html stays identity-encoded. The post-upload curl only warns on
missing `Content-Encoding` (identity still works).

## Headless stress harness

Serve the build dir plus a harness page that shims `requestAnimationFrame`
onto `setTimeout` AND re-implements the global timers on a Web Worker
(Chrome throttles a hidden tab's main-thread timers to ~1/s; worker messages
are exempt), suppresses `<a download>` clicks, passes
`arguments: ['--stress-test=quick', '--stress-report-dir', '/stressout']`
to qtLoad, and polls the report from MEMFS via the exported `FS`. The first
run of a new binary (or port) pays V8 tier-up and is discarded as warm-up;
configs are compared in interleaved pairs on two ports, never sequentially
(machine-load drift beats most effects). The unattended `--run-script` mode
works the same way (write the script into MEMFS from a `preRun` hook).

## Later steps (not built yet)

Step 4 remainder: memory tuning (per-platform undo cap and byte budget,
tile-cache eviction), texture lazy-fetch, the advertised document-size cap,
and preset/library persistence across reloads (user fonts already persist
via the poll-pattern IndexedDB glue in user_fonts_wasm.cpp; follow that
shape, not IDBFS).
