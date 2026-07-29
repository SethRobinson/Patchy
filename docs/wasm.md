# WebAssembly (Emscripten) build

Deep reference for the wasm builds. Read this before touching the `wasm-core`
or `wasm-release` presets, the emsdk/Qt-kit provisioning, or anything under
`scripts/wasm/`.

## What exists today

Two wasm configurations share the pinned Emscripten 3.1.56 toolchain:

- **`wasm-core`** (step 1): the Qt-free engine libraries (`patchy_core`,
  `patchy_render`, `patchy_psd`, `patchy_filters`, `patchy_formats`,
  `patchy_color`, plus `patchy_plugins`, `patchy_lcms2`, `patchy_libraw`) and
  `patchy_core_tests`, run under node. No Qt at all
  (`PATCHY_BUILD_APP=OFF`).
- **`wasm-release`** (steps 2-3): the full app linked against Qt for
  WebAssembly (6.8.3 `wasm_singlethread`, static), booting in a browser tab
  with Asyncify. File open/save/export run through the browser (picker in,
  downloads out), files dragged from the desktop open, and settings persist
  across reloads in localStorage.

Desktop builds are unaffected: the presets, the `if(EMSCRIPTEN)` branches in
CMakeLists, a handful of `Q_OS_WASM` gates in `src/ui`/`src/app`, and the
files under `scripts/wasm/` are the whole wasm surface.

## Toolchain setup

```powershell
pwsh -File scripts\wasm\setup-emsdk.ps1
```

Idempotent. Clones emsdk into `.deps\emsdk` (gitignored, same idea as
`.deps\Qt`) and installs + activates Emscripten 3.1.56. That version is pinned
because it is what the Qt 6.8 documentation lists as supported for Qt for
WebAssembly, so the later Qt-for-wasm work can reuse this toolchain unchanged.
Activation only writes emsdk's local config; nothing touches the user or
system environment. The SDK bundles its own node (22.16.0), which is what runs
the test suite.

## Configure and build

Same wrapper pattern as the Windows release preset: `scripts\vs-env.bat`
supplies cmake and ninja, and `emsdk_env.bat` supplies emcc and its python.
Run from the repository root in PowerShell or cmd, never a POSIX shell:

```powershell
cmd /s /c 'call .deps\emsdk\emsdk_env.bat >nul 2>&1 && call scripts\vs-env.bat -arch=x64 -host_arch=x64 >nul && "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --preset wasm-core'
```

```powershell
cmd /s /c 'call .deps\emsdk\emsdk_env.bat >nul 2>&1 && call scripts\vs-env.bat -arch=x64 -host_arch=x64 >nul && "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset wasm-core'
```

Output lands in `build\wasm-core` (`patchy_core_tests.js` + `.wasm`). The
zero-warning rule applies to this build like any other; vendored-source
suppressions stay scoped to one file and one diagnostic (the only one so far:
miniz's `-Wno-#pragma-messages` in the root CMakeLists, beside its MSVC
`/wd4132`).

## Running the suite

```powershell
pwsh -File scripts\wasm\run-core-tests.ps1
```

Accepts the usual name-substring filter as the first argument. The wrapper
runs the emsdk-bundled node from `build\wasm-core`, so the relative
`test-artifacts/` output lands there, mirroring how the native binaries run
from their build directory. The raw form is:

```powershell
cd build\wasm-core; & ..\..\.deps\emsdk\node\22.16.0_64bit\bin\node.exe patchy_core_tests.js
```

`ctest` also works from `build\wasm-core` because the preset pins
`CMAKE_CROSSCOMPILING_EMULATOR` to the bundled node.

## Preset decisions (all in CMakePresets.json, nothing per-site)

- `-fwasm-exceptions`: the format readers throw `std::runtime_error` as their
  error contract and the test runner catches per test, so exception catching
  must be on (Emscripten disables it by default). Native wasm exceptions are
  used rather than the slower JS-based `-fexceptions`; the bundled node 22
  supports them without flags.
- `-pthread` with `-sPTHREAD_POOL_SIZE=32` and `-sPTHREAD_POOL_SIZE_STRICT=2`:
  the engine has exactly two `std::async` sites (`compositor.cpp` strip
  flatten, `psd_channel_data.cpp` CMYK conversion). Building the runner with
  pthreads keeps them on the production code path with zero source changes;
  output is byte-identical regardless of strip count. The pool is pre-spawned
  and must stay at or above the machine's logical core count, because the CMYK
  site spawns `hardware_concurrency()` workers while the main thread blocks on
  the results; a lazily spawned worker would deadlock there. STRICT=2 turns
  pool exhaustion into a hard error instead of that silent hang, so on a
  machine with more than about 30 logical cores, raise the pool size.
- `-sNODERAWFS=1`: tests read fixtures straight from the real filesystem via
  the `PATCHY_SOURCE_DIR` compile definition, exactly like native runs. This
  keeps the 2.2 GB `local-test-fixtures/` reachable (a preloaded memory
  filesystem could never carry it) and writes `test-artifacts/` to real disk.
  The cost is that the .js is host-bound (node only, no browser), which is
  exactly this preset's scope.
- Heap and stacks: `-sALLOW_MEMORY_GROWTH=1` up to `-sMAXIMUM_MEMORY=4GB`
  (large PSD fixtures), `-sINITIAL_MEMORY=256MB` to cut regrow churn,
  `-sSTACK_SIZE=8MB` for the main thread (LibRaw's dcraw-derived decoders
  carry large stack locals; Emscripten's 64 KB default is far too small) and
  `-sDEFAULT_PTHREAD_STACK_SIZE=1MB` for workers.
- `-sENVIRONMENT=node,worker` (pthread workers need the worker environment)
  and `-sEXIT_RUNTIME=1` so `main`'s return value becomes the process exit
  code and live pool workers do not keep node alive.
- `-Wno-pthreads-mem-growth`: em++ emits an informational link warning that
  pthreads plus a growable heap slows JS-side heap views. The suite computes
  in wasm, not JS, so the note does not apply; suppressing the one diagnostic
  at the one link that emits it keeps the build at zero warnings.
- `--pre-js scripts/wasm/node-env-pre.js`: Emscripten's environ is synthetic
  (USER=web_user and friends) and ignores the host environment, so
  `PATCHY_RENDER_SINGLE_THREADED`, `PATCHY_AF_TRACE`, and the other
  environment escape hatches would silently do nothing under node. The pre-js
  forwards `process.env` before `main` runs, matching native behavior.

## Source-level status

Zero wasm `#ifdef`s in `src/core`, `src/formats`, `src/psd`, `src/render`,
`src/filters`, `src/color`, `src/plugins`. The engine compiled as-is. The one
wasm guard anywhere is in `tests/core/main.cpp`, which skips installing the
crash-stack reporter (Emscripten has neither `execinfo.h` nor fatal-signal
delivery; node prints a wasm stack for traps on its own). One test,
`af_modern_embeds_are_center_anchored_if_available`, self-skips behind a
compile-time `sizeof(void*) < 8` check rather than a platform ifdef; see below.

## Current status (2026-07)

Full `patchy_core_tests` under the bundled node: 726 pass, 0 fail, about 45
seconds wall on a 24-thread machine, with the 2.2 GB `local-test-fixtures/`
corpus present and exercised. The three byte-stability canaries
(`psd_layered_writer_bytes_are_stable`, `gif_encoder_bytes_are_stable`,
`tool_write_paths_digest_baseline`) pass byte-identically, making wasm/musl
the fourth toolchain (after MSVC, Apple clang, GCC/glibc) to produce the
pinned bytes. The `.wasm` is about 6.4 MB. Four tests print `[SKIP]` and
pass:

- `akiko_cycling_okinawa_with_filters`: that local fixture is absent on this
  machine; unrelated to wasm.
- Two HEIC tests: no platform HEIC decoder outside Windows; the stub path is
  the intended behavior.
- `af_modern_embeds_are_center_anchored_if_available`: the tier-1 import of
  `restaurant-menu-inside.af` (a wild Affinity menu whose spreads hold a
  dozen 75-150 MB scan layers live at once) exceeds a 32-bit address space.
  Under wasm32 the reader throws `std::bad_alloc` and falls back to its
  embedded preview, which has no swash layer to assert on, so the test skips
  on 32-bit targets. This is the first concrete sighting of the wasm32
  document-size ceiling the feasibility work predicted; step 4 turns that
  ceiling into a measured, advertised cap.

## The app build (wasm-release)

### Provisioning

```powershell
pwsh -File scripts\wasm\setup-emsdk.ps1
pwsh -File scripts\wasm\setup-qt-wasm.ps1
```

The second script installs Qt 6.8.3 `wasm_singlethread` (plus qtimageformats)
into `.deps\Qt\6.8.3\wasm_singlethread` via aqtinstall (venv under
`.deps\aqt-venv`). Host tools (moc/rcc/lrelease) come from the vendored
`msvc2022_64` kit through `QT_HOST_PATH`; the preset chains the Qt toolchain
file into emsdk's via `QT_CHAINLOAD_TOOLCHAIN_FILE`.

### Build and run

Configure and build exactly like `wasm-core` but with `--preset wasm-release`
(same `emsdk_env.bat` + `vs-env.bat` wrapper). Output:
`build\wasm-release\patchy.html`, `patchy.js`, `patchy.wasm`, `patchy.data`
(preloaded fonts/translations/bundled scripts), `qtloader.js`. Serve and
open:

```powershell
pwsh -File scripts\wasm\serve-app.ps1
```

then browse to `http://localhost:8973/patchy.html`.

### Decisions and gates (step 2)

- **Asyncify + JS exceptions.** `-sASYNCIFY -Os` on the app link makes the
  nested-event-loop sites (`QDialog::exec`, `run_non_modal_dialog`,
  `processEvents` pumps) work unmodified. Asyncify does not support
  wasm-native exception handling, so the app preset compiles with
  `-fexceptions` (JS-based); `wasm-core` keeps the faster `-fwasm-exceptions`
  for the test runner, which never links Qt.
- **Single-threaded.** The kit is `wasm_singlethread`; the browser build must
  never create a thread. The seams: `run_tracked_background_worker` and
  `launch_async` in `ui/background_workers.hpp` run work inline on wasm (the
  ready-future shape keeps the `wait_for == ready` event pumps working; never
  swap those sites to `std::launch::deferred`, which those pumps would spin
  on forever). The compositor and `image_document_io` strip renderers
  self-serialize at `hardware_concurrency()==1`, the CMYK site in
  `psd_channel_data.cpp` has a portable one-worker inline path, and the
  script watchdog thread is not spawned (stuck scripts cannot be interrupted;
  nothing could preempt `evaluate()` on this platform anyway).
- **Compiled out or stubbed:** QtPrintSupport does not exist on wasm, so
  `print_dialog.cpp` is replaced by `print_dialog_wasm.cpp` stubs and the
  File menu hides Print/Page Setup (the portable placement/render half lives
  in `print_layout.cpp` everywhere). The single-instance QLocalServer path is
  off (one tab is one instance). The update check is gated off everywhere:
  the startup check returns early, the About dialog skips
  `begin_update_check` (its status label keeps "Patchy is ready."), and
  Preferences hides the "Check for updates on startup" checkbox (the site
  redeploy is the update mechanism; the GitHub fetch would only fail CORS).
  Script sound effects are silent no-ops (no QProcess in the
  browser). Scanner import was already platform-gated off.
- **Assets.** `--preload-file` mounts the staged build-dir copies at
  `/fonts`, `/translations`, and `/scripts` (~1.1 MB packed into
  `patchy.data`). `applicationDirPath()` is `/` on wasm, so the existing
  directory probes in `main.cpp` and `localization.cpp` find them with zero
  code changes. `qtbase_ja.qm` is staged from the host kit (the wasm kit
  ships no `.qm`). The 8.7 MB texture pack stays embedded in the binary for
  now; moving it to lazy HTTP fetch is a step 4 size lever.
- `QT_WASM_INITIAL_MEMORY` is 512 MB with growth to a 4 GB cap. Qt's target
  machinery owns `-sSTACK_SIZE`; do not add a second one.

### Step-2 status (2026-07)

The app links, loads, and boots to a fully painted start panel in the
browser: menu bar, tool palette with all SVG icons, brush options bar,
Layers/Channels/Paths docks, theme QSS, and the preloaded fonts all render.
The boot produces zero browser console errors. Pointer input works (menus
open, track, and dismiss), and the hidden Print/Page Setup actions confirm
the wasm gating (they exist for hotkey-id stability but do not lay out).
Sizes: `patchy.wasm` 70.1 MiB uncompressed (static Qt + Asyncify + the
8.7 MB embedded texture pack; compression and lazy texture fetch are step 4),
`patchy.data` 1.1 MiB, `patchy.js` 356 KiB. Local load-to-painted-UI is a few
seconds.

Testing note: a background/hidden browser tab never fires
requestAnimationFrame, so Qt stops presenting new frames until the tab is
visible. Keep the tab foregrounded when testing; nothing is wrong with the
app when a hidden tab looks frozen. For automated testing in a hidden tab,
shim requestAnimationFrame onto setTimeout before qtloader runs (the step-3
verification used a build-dir copy of patchy.html with exactly that shim).

## Web file access (step 3)

Design: real files never leave the browser sandbox, so both directions stage
through MEMFS and the whole path-based pipeline runs unchanged. Opens copy
the picked or dropped bytes to `/opened/<n>/<name>` (or `/dropped/<n>/<name>`)
and hand that path to `open_document_path`; saves let the existing writers
write `/saved/<n>/<name>` (or the document's current MEMFS path) and then hand
the written bytes to the browser as a download. All glue lives in
`src/ui/dialog_utils_wasm.{hpp,cpp}`; `dialog_utils.cpp`'s three pickers
delegate to it under `Q_OS_WASM`, and `offer_browser_download_for_saved_file`
(no-op on desktop) is called after each successful write in the save, export,
sprite-sheet, smart-object, Curves-preset, gradient-export, and script-editor
paths.

Two platform findings constrain the shape; do not regress them:

- **A JS promise cannot complete into a nested event loop.** With the app
  suspended in `QEventLoop::exec` (Asyncify), DOM events still re-enter the
  module through Qt's event queue, but qstdweb promise callbacks never
  arrive, so `QFileDialog::getOpenFileContent` deadlocks if anything blocks
  on it. The picker therefore uses its own `<input type=file>` whose change
  and cancel handlers are pure page-side JS writing a plain JS object, and
  the blocked C++ side polls that object with a QTimer (timers reliably
  resume the suspended loop). Never wait on a Qt async JS API from a nested
  loop.
- **Qt 6.8 wasm cannot receive external drops.** It never registers browser
  dragover/drop listeners (only dragstart/dragleave/dragend for its own
  drags), so the browser never delivers a desktop file drop to Qt. The glue
  installs page-side dragover/drop listeners itself
  (`install_web_drop_target`, wired in the MainWindow constructor), reads the
  dropped files in JS, and reports them one MEMFS path at a time to
  `MainWindow::handle_web_file_drop`. The desktop `QDropEvent` path stays
  untouched and simply never fires on wasm.

The other step-3 decisions:

- **Save As and Export prompt for a name and format** in a small dialog
  (`wasmSaveFileDialog`) instead of a file dialog; the chosen filter row
  flows back through `selected_filter`, so `path_with_default_extension` and
  every caller work unchanged. Save re-downloads under the current name;
  the session is marked clean after the MEMFS write.
- **Downloads use our own Blob + `<a download>` anchor click**, not
  `QFileDialog::saveFileContent`: the Chrome save picker behind that API
  needs transient user activation (a long PSD write outlives it) and can be
  cancelled with no signal after the session was already marked saved. The
  anchor form works identically everywhere and cannot fail silently.
- **Settings persist in localStorage.** `app_settings()` uses
  `QSettings::WebLocalStorageFormat` on wasm (the backend is synchronous with
  an empty `flush()`; there is no async hazard). `IniFormat` would land in
  MEMFS and evaporate on reload. Keys look like `qt-v0-Patchy-Patchy-<key>`.
- **Preset libraries re-seed each session.** Library files live under
  `/presets/<subdir>` in MEMFS (the localStorage backend has no `fileName()`
  directory) and vanish on reload while the seeding stamps persist, so
  `stored_default_asset_version` (main_window_tool_options.cpp) treats every
  wasm session as unseeded. Defaults return on reload, user-created presets
  last one session; real persistence (IDBFS/OPFS) is a step-4 candidate.
- **One picker at a time**: a second Open while the browser chooser is up
  would nest a second Asyncify suspend and hang the runtime; the wait dialog
  is modal and `pick_open_file` carries a re-entrancy guard.
- Recent Files/Folders are left as-is: fully functional within a session
  (MEMFS paths), and the existing startup pruning self-empties them after a
  reload. Multi-file pickers degrade to a single pick (no multi-file content
  picker on wasm). Export Layers as Image Sequence is hidden (one download
  per layer reads as download spam and browsers block it).

### Step-3 status (2026-07)

Verified in Chromium against the built binary through a scripted harness
(synthetic events; downloads and chooser clicks intercepted and inspected):
open a real PSD through the picker (parse, session, recents), cancel the
picker cleanly, re-open immediately, double-open guarded, Save produces a
download whose bytes match the written MEMFS file, Save As round-trips the
prompt into `/saved/<n>` plus a download and repaths the session, a dropped
PNG opens as a document, settings and tool options survive reloads, and the
preset libraries re-seed identically each boot. Zero console errors and
zero-warning builds throughout. Not yet exercised by hand: a visible-tab
click-through (menus, the real OS file chooser, a real Downloads-folder
save) and the Firefox/Safari input fallback; both are quick manual passes.

Known not-working (by design at this step): preset persistence across
reloads (defaults re-seed; user presets last one session), the preset
managers' raw QFileDialog import/export buttons (they browse MEMFS),
image-sequence export (hidden), script-editor plain Save downloads nothing
(Save As does), "Open in File Explorer" on recents does nothing, no printing
or PDF export, no scanner import, no float windows (documents stay tabbed;
see below), silent script sounds, and no stuck-script watchdog. Documents
above roughly the wasm32 memory ceiling fail to open; step 4 turns that into
an advertised cap.

## Browser UI fit

Wasm-only accommodations for living inside a single browser canvas:

- **The start panel carries a web-version note** (`startPanelWasmNote`,
  start_panel.cpp): everything runs locally in the browser and nothing is
  sent online, with a link to the GitHub download table for the desktop
  build (system fonts, speed).
- **Float windows are disabled.** A browser tab is one window: Qt for
  WebAssembly has no window manager and no `startSystemMove`, so a floated
  document covered the whole canvas with no way to move, dock, or reach the
  UI underneath. `MainWindow::float_document_session` no-ops on wasm; it is
  the single funnel for every entry point (Window menu, hotkeys, tab context
  menu, tab tear-off, Float All, Tile/Cascade), so that one guard turns the
  feature off. The six window-arrangement actions and the tab context menu's
  Float item are hidden but stay registered for hotkey-id stability (the
  Print pattern), and the tab tear-off gesture is compiled out of the
  MainWindow event filter so vertical drift during a tab reorder no longer
  ends the drag with tear-off's synthetic release. See
  [float-windows.md](float-windows.md).
- **The main window stays on the bottom of the z-stack.** Qt's wasm
  compositor keeps one z-stack for all top-level windows and raises whichever
  window is clicked, so a canvas click lifted the fullscreen main window above
  a non-modal dialog (layer style, curves): the dialog vanished with no window
  manager to recover it while its preview edit lock stayed engaged, wedging
  the app. Modal dialogs never had the problem (the plugin redirects
  activation to the blocking modal window). The MainWindow constructor sets
  `Qt::WindowStaysOnBottomHint` on wasm, which parks the main window in the
  compositor's stay-on-bottom stacking zone: clicking it still activates and
  focuses it, but the raise becomes a no-op, and dialogs and popups keep their
  normal order in the regular zone above.
- **Dialogs clamp to the canvas.** Desktop window managers keep an oversized
  dialog's chrome reachable; the browser canvas has nothing equivalent, so a
  dialog taller than the canvas left its OK/Cancel row unreachable below the
  page fold. On wasm `place_dialog` (dialog_utils.cpp, the funnel under
  `exec_dialog`/`run_non_modal_dialog`) shrinks the dialog, and any explicit
  minimum size above the canvas, to the available screen before placement.
  Verified in the browser: at a 1000x620 canvas the Filter Gallery opens at
  992x588 with its Apply/Cancel row on screen, instead of its requested
  1120x720. When even the dialog's LAYOUT minimum exceeds the canvas,
  `install_dialog_overflow_scroll` moves the content into a
  `QScrollArea` (`dialogOverflowScroll`) so the shrink can proceed and the
  button row stays reachable by scrolling; dark-chrome dialogs keep the title
  bar fixed and scroll the content area below it, other dialogs scroll whole.
  One limit is deliberate: the clamp runs at placement time only, so
  shrinking the browser window while a dialog is already open does not re-fit
  it. Desktop placement is untouched. This is also why a dialog should be
  shown through `exec_dialog`/`run_non_modal_dialog` rather than a bare
  `QDialog::exec`; the brush-tip, pattern, and style preset managers were
  converted for it.

## Release deployment (rtsoft.com/patchy)

The web build is part of the standard release flow; the batch-file details live
in [release-process.md](release-process.md). Short version:
`scripts\release\build-wasm.bat` (run by `release-all.bat`) builds the
`wasm-release` preset and stages the deployable files into
`build\package\wasm-site`; `scripts\release\start-local-wasm-server.bat`
serves that staged payload for a browser check (it stops a server left over
from a previous run, then opens the site in the default browser);
`scripts\release\upload-wasm-to-rtsoft.bat` (run by `upload-to-rtsoft.bat`)
publishes it to `rtsoft.com/patchy` over ssh/scp.

The deployed page is not Qt's generated `patchy.html` shell (that one still
lands in `build\wasm-release` and serves the dev loop via `serve-app.ps1`).
The site stages a Patchy-branded shell configured from
`packaging\web\patchy.html.in`: logo, app name and version, and a real
download progress bar. Emscripten exposes no download-progress callback, so
the page fetches `patchy.wasm` itself with a byte-counting stream reader and
hands the bytes to `qtLoad` as `wasmBinary`; `qtLoad` forwards unknown config
keys to the Emscripten module factory, which is also how the page's
`locateFile` override versions the `patchy.data` fetch. If a transfer hides
the total size (e.g. compressed encoding), the bar switches to an
indeterminate sweep rather than showing a wrong percentage.

Caching: `build-wasm.bat` stamps a per-build cache tag (app version plus
timestamp) into every asset reference (`?v=<tag>`), and the staged `.htaccess`
(source: `packaging\web\.htaccess`) marks html `no-cache` while tagged assets
cache forever. A redeploy therefore shows up on the next page load; no
hard-refresh needed. All `.htaccess` directives are IfModule-guarded, so a
host missing `mod_headers` serves the site without caching hints instead of
failing. MIME needs nothing special: if a server does not send
`application/wasm`, Emscripten falls back from streaming to ArrayBuffer
instantiation (with `wasmBinary` supplied, streaming is not used anyway).
Compressed serving is a step-4 lever. The shell page's text is static html
outside the Qt localization system and stays English.

## Later steps (not built yet)

- Step 4: memory tuning (per-platform undo cap and byte budget, tile-cache
  eviction), optional threaded rendering behind COOP/COEP hosting, texture
  lazy-fetch and compressed packaging/deployment, the measured document-size
  cap the web build advertises, and preset/library persistence (IDBFS or
  OPFS) so user presets survive reloads.
