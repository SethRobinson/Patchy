# WebAssembly (Emscripten) build

Read before changing wasm presets, emsdk/Qt provisioning, or `scripts/wasm/`.

## What exists today

All three use Emscripten 4.0.7:

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

Stop every server you started when done (AGENTS.md rule, `free-server-port.ps1`).

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
  Desktop Version" download button (see [pdf.md](pdf.md)); the G'MIC row is a
  stub.
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
  embedded. Object Selection's runtime and models are fetched on first
  use ([ai-selection.md](ai-selection.md)).
- **Memory:** the shell page constructs the shared `WebAssembly.Memory`
  and passes it to qtLoad as `wasmMemory`; `QT_WASM_INITIAL_MEMORY`
  (256 MB) is the FLOOR baked into the memory import, and the page's
  `BAKED_MIN_MB` must stay in sync (a smaller initial is a LinkError).
  Ladders, the About readout, `patchyMemStats`, the history/cache budgets,
  and the Safari 26 tab-kill investigation: [wasm-memory.md](wasm-memory.md).

### Web file access, threads and blocking

File open/save in the browser, the worker threads and the blocking rules are in [wasm-files-threads.md](wasm-files-threads.md).

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
- **Right panel toggles repaint the whole window.** The wasm backing
  store keeps stale pixels where a relayout moved content, so `create_docks`
  calls `update()` whenever a panel expands, collapses, closes or reopens
  ([right-docks.md](right-docks.md)); the column edge and dividers are
  `QSplitter` handles, which rely on Qt's implicit press-grab (explicit
  `grabMouse` is unreliable here).
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

## Release deployment (rtsoft.com/patchy)

Batch-file details live in [release-wasm.md](release-wasm.md):
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
[release-wasm.md](release-wasm.md). Every asset is same-origin, so no
per-asset CORP headers are needed.

The deployed page is a Patchy-branded shell from
`packaging\web\patchy.html.in`, not Qt's generated `patchy.html` (the
dev-loop page; staging, cache tags, and the compressed `.br`/`.gz` serving
negotiation are in [release-wasm.md](release-wasm.md)). Emscripten
has no download-progress callback, so the page fetches `patchy.wasm` itself
with a byte-counting reader into one preallocated exact-size buffer,
compiles it with `WebAssembly.compile`, and passes the module to `qtLoad`
as `qt.module`, so the fetched bytes are collectable after compile
(`wasmBinary` would be glue-retained for the session, ~66 MB). The page
also constructs the memory (bullet above), appends a plain-language hint to
the crash screen when the abort text looks like out-of-memory, and versions
the `patchy.data` fetch via `locateFile`. No special MIME is needed (the
page compiles from bytes; streaming instantiation is unused).
