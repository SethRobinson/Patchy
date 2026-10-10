# WebAssembly (Emscripten) build

Read before changing wasm presets, emsdk/Qt provisioning, or `scripts/wasm/`.

## What exists today

All three configurations use Emscripten 4.0.7:

- **`wasm-core`**: the Qt-free engine libraries plus `patchy_core_tests`,
  run under node (`PATCHY_BUILD_APP=OFF`).
- **`wasm-release`**: the full app with static Qt 6.10.3 `wasm_multithread`,
  Asyncify and pthreads. File I/O, drops and settings are browser-backed.
  Real worker threads require COOP/COEP headers (see deployment).
- **`wasm-release-st`**: 6.10.3 single-thread kit, `PATCHY_WASM_SINGLETHREAD=ON`
  (no pthreads, pool or shared memory). Manual comparisons only; releases use
  threads even on Safari ([wasm-memory.md](wasm-memory.md)). Provision with
  `setup-qt-wasm.ps1 -WasmArch wasm_singlethread`. ST lacks the declared
  `QThread::loopLevel()` definition; `canvas_widget_move.cpp` instead reads
  `QThreadData` through `Qt6::CorePrivate`.

Stress/A-B harness: [performance.md](performance.md).

## Toolchain setup

```powershell
pwsh -File scripts\wasm\setup-emsdk.ps1
```

Clones or updates `.deps\emsdk` (gitignored), then installs and activates
Qt-supported Emscripten 4.0.7. `-EmsdkVersion` selects another version;
activation replaces `upstream/`, so serialize versions and restore 4.0.7
afterward. Tests use bundled node 22.16.0. The scripts glob
`.deps\emsdk\node\*\bin\node.exe`; exactly one must match (newer node
packages omit `bin\`).

## Configure and build (wasm-core)

From the repo root in PowerShell/cmd, never a POSIX shell: `scripts\vs-env.bat`
supplies cmake/ninja; `emsdk_env.bat` supplies emcc.

```powershell
cmd /s /c 'call .deps\emsdk\emsdk_env.bat >nul 2>&1 && call scripts\vs-env.bat -arch=x64 -host_arch=x64 >nul && "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --preset wasm-core'
```

Build: same wrapper, `--build --preset wasm-core`, zero warnings. Output:
`build\wasm-core\patchy_core_tests.js` + `.wasm`.

## Running the suite

```powershell
pwsh -File scripts\wasm\run-core-tests.ps1
```

Accepts a name-substring filter; runs bundled node from `build\wasm-core`,
with `test-artifacts/` there. `ctest` uses the pinned `CMAKE_CROSSCOMPILING_EMULATOR`.

Canaries match native output. Expected `[SKIP]`s: absent local fixtures, two
HEIC tests (node lacks `VideoDecoder`), and
`af_modern_embeds_are_center_anchored_if_available` (beyond wasm32's address
space). `tests/core/main.cpp` skips the native crash-stack reporter on wasm.

`psd_testy_legacy_fills_and_masks_round_trip_if_available` checks both imports;
C2Kyoto's 415-layer save/readback requires 64-bit pointers (wasm32's 4 GB limit).
Icon and synthetic legacy-fill/mask round trips run on every platform.

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
- `-sNODERAWFS=1`: Node-only host I/O reads fixtures via `PATCHY_SOURCE_DIR`
  and writes `test-artifacts/`. `weakly_canonical` (`fs::relative`,
  `fs::canonical`) rejects existing Windows `D:/...` paths with "No such file
  or directory". Use `lexically_relative`/`lexically_normal` for constructed
  paths. `fs::copy_file` to an existing file fails with "Bad file descriptor";
  remove the target first.
  Test cleanup uses Node `path`/`fs`: musl rejects Windows cwd paths and cannot
  remove directory links. Resolve host-absolute targets/temp roots and parent
  links; retain missing suffixes/final leaves, check containment, then use `rmSync`.
  Refuse resolution errors/dangling parents; never fall back lexically.
  Native suites retain standard-filesystem cleanup.
  Atomic-write hard-link fixtures use Node `fs.linkSync`: Emscripten does not
  implement `link`/`linkat`. The production writer still validates the host
  file's link count through NODERAWFS; these assertions must not be skipped.
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
kit (moc, rcc, lrelease, lupdate, and the staged `qtbase_<code>.qm` files come from it;
verified after install). `-WasmArch wasm_singlethread` and `-QtVersion`
select other kits; kits coexist under `.deps\Qt\<version>\`, so rollback is
a preset edit. Desktop presets stay on their vendored 6.8.3 kit. The preset
chains Qt's toolchain file into emsdk's via `QT_CHAINLOAD_TOOLCHAIN_FILE`.

Not 6.11 yet: aqtinstall 3.3.0 cannot install a 6.11 desktop host kit
(per-arch folders, no base `Updates.xml`).

Kit facts: 6.10 suspends via `EM_ASYNC_JS` (no `-sASYNCIFY_IMPORTS`);
Emscripten 3.1.58+ folds the pthread bootstrap into `patchy.js`.

### Build, serve, stop

Configure and build like `wasm-core` but with `--preset wasm-release`.
Output: `build\wasm-release\patchy.html`, `patchy.js`, `patchy.wasm`,
`patchy.data` (preloaded fonts/translations/scripts), `qtloader.js`. Serve
locally:

```powershell
pwsh -File scripts\wasm\serve-app.ps1   # [port] [--open]; default port 8973
```

then open `http://localhost:8973/patchy.html`; `serve.mjs` sends the
COOP/COEP headers the threaded build needs. `scripts\release\start-local-wasm-test-server.bat`
(raw build dir) and `start-local-wasm-server.bat` (staged site) add port cleanup.

When wasm work is finished, stop every local server you started with
`scripts\wasm\free-server-port.ps1 -Port <port>`, one call per port (repo
rule; see AGENTS.md).

A hidden tab never fires requestAnimationFrame, so Qt stops presenting and
the tab looks frozen. Keep it foregrounded, or shim requestAnimationFrame
onto setTimeout before qtloader runs (harness below).

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
  crash. 1 MiB per live suspend is a few MB worst case. Qt owns
  `-sSTACK_SIZE`; do not add a second one.
- **CLI flows exit through `exit_cli_application` (ui/cli_exit.hpp), never
  a bare `QCoreApplication::exit`.** With Emscripten's default
  EXIT_RUNTIME=0 a bare exit unwinds the Asyncify-resumed exec stack, main
  returns, and the runtime silently stays alive with Qt destroyed: qtloader's
  onExit never fires and the tab parks with a clean console. On wasm the
  helper calls `emscripten_force_exit` (workers stop, Module.onExit delivers
  the code as `qt.onExit`, destructors and unflushed settings are skipped).
  Used by every CLI completion; desktop keeps `QCoreApplication::exit`.
- **JSPI needs a source-built Qt** with wasm EH and `-feature-wasm_jspi`.
  Stock Qt's JS-exception libraries cannot mix with it, so the shipped aqt
  kit stays on Asyncify.
- **Codegen: compile `-msimd128`, link `-O3`, `-sMALLOC=dlmalloc`.** SIMD wins
  5-16% on compute steps (canaries stay byte-identical) and `-O3` beats `-Os`
  at runtime. `PATCHY_WASM_ALLOCATOR=mimalloc` is a benchmark option only
  (it exhausts wasm32's 4 GB on the 350 MB C2Kyoto PSD; [wasm-memory.md](wasm-memory.md)).
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
- **Clipboard:** reads are cached, image writes bypass Qt's untyped Blob
  ([clipboard.md](clipboard.md)).
- **Compiled out or stubbed:** QtPrintSupport does not exist on wasm
  (`print_dialog_wasm.cpp` stubs; File menu hides Print/Page Setup; the
  portable half stays in `print_layout.cpp`). Qt publishes no wasm qtpdf
  either, so `pdf_import_stub.cpp` builds instead of `pdf_import.cpp` and
  `file_format_entries()` drops `.pdf` from the open filter; PDF EXPORT still
  works, because `pdf_export.cpp` only needs QtGui's QPdfWriter. A picked or
  dropped `.pdf` still enters the open pipeline and gets the stub's
  marker-tagged error, which the open-failed box turns into a "Get the
  Desktop Version" download button (see [pdf.md](pdf.md)).
  Deep editing stops at 16 bits (`depth_supported_on_platform`): 32-bit
  files open converted to 16 and Image > Mode and New Document offer no 32
  ([high-bit-depth.md](high-bit-depth.md)). Single-instance QLocalServer
  off. Update check off (the site redeploy is the update mechanism; the
  GitHub fetch would fail CORS). Script sounds no-op. Scanner import off.
  File > Export > Layers as Image Sequence hidden. Multi-file pickers degrade
  to one pick. Browser imports stay out of Recents because their source is
  released after load. Script-editor plain Save downloads nothing (Save As does).
- **Assets.** `--preload-file` mounts staged copies at `/fonts`,
  `/translations`, `/scripts` inside `patchy.data`; `applicationDirPath()`
  is `/`, so existing directory probes work unchanged. The `qtbase_<code>.qm`
  files are staged from the host kit (the wasm kit ships no `.qm`).
  `third_party/fonts-web` (~23 MB of OFL fonts, wasm only; [fonts.md](fonts.md))
  merges into the staged fonts; `LINK_DEPENDS` on the fonts stamp makes a
  fonts-only change repack `patchy.data`. The 8.7 MB texture pack stays
  embedded.
- **Memory:** the shell page constructs the shared `WebAssembly.Memory`
  and passes it to qtLoad as `wasmMemory`; `QT_WASM_INITIAL_MEMORY`
  (256 MB) is the FLOOR baked into the memory import, and the page's
  `BAKED_MIN_MB` must stay in sync (a smaller initial is a LinkError).
  Ladders, the About readout, `patchyMemStats`, the history/cache budgets,
  and the Safari 26 tab-kill investigation: [wasm-memory.md](wasm-memory.md).

### Web file access

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

### Browser UI fit

- **Desktop download card** (web start panel only): below New Document / Open;
  its button opens the GitHub README's download section in a new tab. Text
  retranslates live and wraps; short windows scroll the content above a fixed
  footer. Privacy and font-upload guidance follows the card.
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
  floated document would cover the canvas with no way back.
  `MainWindow::float_document_session` no-ops on wasm and is the single
  funnel for every entry point. The window-arrangement actions stay
  registered but hidden (hotkey-id stability, like Print); the tab tear-off
  gesture is compiled out. See [float-windows.md](float-windows.md).
- **Non-modal dialogs are restacked above their parent.** The wasm
  compositor raises a clicked window without its transient children.
  `keep_dialog_above_parent_window` (dialog_utils.cpp) registers every
  dialog `run_non_modal_dialog` shows and restacks a window's registered
  dialogs above it one event-loop turn after a press or activation reaches
  it, walking the parent chain. Never set `Qt::WindowStaysOnBottomHint` on
  the main window: it sends main-window-parented dialogs to the bottom zone.
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
  bar fixed. The size clamp runs at initial placement only; the later
  Show/Resize recheck (every platform, [ui-conventions.md](ui-conventions.md))
  only moves the frame back on screen. This is also why dialogs
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
  keystroke again (script canvas windows created under the stop panel).
  Guards (script host): `createCanvas`
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
