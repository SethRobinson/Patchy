# WebAssembly (Emscripten) build

Deep reference for the wasm builds. Read this before touching the `wasm-core`
or `wasm-release` presets, the emsdk/Qt-kit provisioning, or anything under
`scripts/wasm/`.

## What exists today

Two wasm configurations share the pinned Emscripten 4.0.7 toolchain:

- **`wasm-core`** (step 1): the Qt-free engine libraries (`patchy_core`,
  `patchy_render`, `patchy_psd`, `patchy_filters`, `patchy_formats`,
  `patchy_color`, plus `patchy_plugins`, `patchy_lcms2`, `patchy_libraw`) and
  `patchy_core_tests`, run under node. No Qt at all
  (`PATCHY_BUILD_APP=OFF`).
- **`wasm-release`** (steps 2-4a): the full app linked against Qt for
  WebAssembly (6.10.3 `wasm_multithread`, static), booting in a browser tab
  with Asyncify plus pthreads. File open/save/export run through the browser
  (picker in, downloads out), files dragged from the desktop open, and
  settings persist across reloads in localStorage. Previews, the compositor
  strip renderers, and every background worker run on real threads exactly
  like the desktop builds; the deployment requirement that buys this is
  cross-origin isolation (COOP/COEP headers, see below).

Desktop builds are unaffected: the presets, the `if(EMSCRIPTEN)` branches in
CMakeLists, a handful of `Q_OS_WASM` gates in `src/ui`/`src/app`, and the
files under `scripts/wasm/` are the whole wasm surface.

## Toolchain setup

```powershell
pwsh -File scripts\wasm\setup-emsdk.ps1
```

Idempotent, and takes `-EmsdkVersion` to provision a different release
(versions install side by side; `activate` selects). Clones emsdk into
`.deps\emsdk` (gitignored, same idea as `.deps\Qt`), refreshes an existing
clone with `git pull` (a stale checkout does not know newer releases and fails
the install with "unknown version"), and installs + activates Emscripten
4.0.7. That version is pinned because it is what the Qt 6.10 and 6.11
documentation lists as supported for Qt for WebAssembly. Activation only
writes emsdk's local config; nothing touches the user or system environment.
The SDK bundles its own node (22.16.0), which is what runs the test suite;
`.deps\emsdk\.emscripten` names the activated one, and the scripts glob
`node\*\bin\node.exe`, so keep exactly one node directory there.

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
  and must cover the largest blocking fan-out, because those sites spawn
  workers while the calling thread blocks on the results; a lazily spawned
  worker would deadlock there. Both sites cap at 16 workers (the CMYK cap
  landed with the step-4a app threading), so 32 has comfortable headroom on
  any machine. STRICT=2 turns pool exhaustion into a hard error instead of
  that silent hang.
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

Full `patchy_core_tests` under the bundled node: 736 pass, 0 fail, about 45
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

The second script installs Qt 6.10.3 `wasm_multithread` (plus qtimageformats)
into `.deps\Qt\6.10.3\wasm_multithread` via aqtinstall (venv under
`.deps\aqt-venv`, upgraded on every run so it knows current Qt releases), and
installs the matching `win64_msvc2022_64` host kit beside it when missing.
`QT_HOST_PATH` must be the same Qt version as the wasm kit: moc, rcc, lrelease
and the `qtbase_ja.qm` the build stages all come from it, so the script
verifies those three after installing. Pass `-WasmArch wasm_singlethread` for
the single-threaded kit or `-QtVersion` for another release; kits coexist side
by side under `.deps\Qt\<version>\`, which also makes rollback a preset edit.
The desktop presets stay on their own vendored 6.8.3 kit. The preset chains
the Qt toolchain file into emsdk's via `QT_CHAINLOAD_TOOLCHAIN_FILE`.

**Not 6.11 yet:** released aqtinstall (3.3.0) cannot install a 6.11 *desktop*
host kit. download.qt.io restructured the 6.11 desktop repository into
per-arch folders (`qt6_6111_msvc2022_64`) with no base `qt6_6111/Updates.xml`,
and aqt still requests the base file, so the version-matched host kit is
unobtainable. Revisit when aqtinstall understands the new layout.

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
  for the test runner, which never links Qt. Asyncify coexists with pthreads:
  the 2026-07 migration verified dialogs opening and closing into nested
  loops with the worker pool live and zero console errors.
- **`-sASYNCIFY_STACK_SIZE=1048576` is load-bearing.** Emscripten's default is
  4 KB, which is the buffer Asyncify saves the unwound call stack into on each
  suspend. The main loop's idle suspend is shallow and fits; a dialog opened
  from inside another dialog's nested `exec` suspends the whole UI call chain
  and overflows it. Overflow is not diagnosed in a non-assertion build: the
  asyncify runtime executes an `unreachable`, which surfaced as
  `Aborted(RuntimeError: unreachable)` from `maybeStopUnwind`/`doRewind`
  followed by `memory access out of bounds`, i.e. a hard tab crash the moment
  the Layer Style dialog opened a color picker (2026-07-31). 1 MiB is
  allocated per live suspend, so the worst case is a few MB against a 512 MB
  initial heap. Qt 6.8 suspended through its own `qt_asyncify_suspend_js`
  import; 6.10 rewrote that as a plain `EM_ASYNC_JS` await, which is why the
  default only became too small on the newer kit.
- **Link optimization stays `-Os`.** The 2026-07 trial measured `-O3` at
  74,029,730 bytes against 73,449,183 for `-Os` on the single-threaded build
  (+0.8%) with no demonstrable runtime win and a slower wasm-opt pass, so
  size keeps the tiebreak. `-msimd128` was trialed the same day on
  `wasm-core`: all 726 tests pass and the three byte-stability canaries hold
  byte-identically, but the full suite only improved from 47.5-47.6 s to
  46.4-46.6 s (about 2%, below the adoption bar) while adding 83 KB, so it
  was rejected; the canary result means it can be revisited cheaply if a
  compute-heavy in-app benchmark ever argues for it.
- **PATCHY_* escape hatches work in the browser.** The app link carries
  `--pre-js scripts/wasm/app-env-pre.js`, which copies `PATCHY_*` keys from
  the page URL's query string into the Emscripten environment before `main`
  runs (the node runner's `node-env-pre.js` does the same from
  `process.env`). `patchy.html?PATCHY_RENDER_SINGLE_THREADED=1` is the
  in-build control group for threading comparisons; only `PATCHY_`-prefixed
  keys are forwarded.
- **Multithreaded (since step 4a, 2026-07; originally shipped
  single-threaded).** The kit is `wasm_multithread`: pthreads on a
  SharedArrayBuffer heap, which browsers only enable on cross-origin isolated
  pages (the serving requirements live in the deployment section). The
  threading seams in `ui/background_workers.{hpp,cpp}` and the script
  watchdog in `ui/script_engine.cpp` are gated on
  `defined(Q_OS_WASM) && !defined(__EMSCRIPTEN_PTHREADS__)`, so this build
  takes the real-thread branches like the desktop platforms, and a
  single-threaded wasm build (the seams stay for it) still runs work inline.
  The compositor and `image_document_io` strip renderers need no gate at all;
  they read `hardware_concurrency()`, which now reports the visitor's core
  count. Previews therefore render off the main thread, cooperative
  cancellation and the processing spinner work, and the script watchdog can
  interrupt stuck scripts. If the inline fallback ever returns, remember the
  ready-future shape keeps the `wait_for == ready` event pumps working; never
  swap those sites to `std::launch::deferred`, which those pumps would spin
  on forever.
- **Pthread pool sizing.** `pthread_create` cannot finish lazily while the
  spawning thread blocks, and Edit > Flatten runs the strip compositor on the
  browser main thread and immediately joins, so the pool must pre-spawn the
  largest blocking fan-out. The CMYK site in `psd_channel_data.cpp` and both
  strip renderers are capped at 16 workers; the app sets
  `QT_WASM_PTHREAD_POOL_SIZE` to the JS expression
  `Math.min(navigator.hardwareConcurrency,16)+8` (evaluated at page load), 16
  for the worst join fan-out plus headroom for concurrently live
  preview/open/undo workers. `PTHREAD_POOL_SIZE_STRICT` is deliberately not
  set: overflow lazily spawns (fine from worker threads, which yield)
  instead of aborting a visitor's session. Workers get 4 MB stacks
  (`-sDEFAULT_PTHREAD_STACK_SIZE`); LibRaw decode and full compositor walks
  run there now.
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

Three platform findings constrain the shape; do not regress them:

- **A JS promise cannot complete into a nested event loop.** With the app
  suspended in `QEventLoop::exec` (Asyncify), DOM events still re-enter the
  module through Qt's event queue, but qstdweb promise callbacks never
  arrive, so `QFileDialog::getOpenFileContent` deadlocks if anything blocks
  on it. The picker therefore uses its own `<input type=file>` whose change
  and cancel handlers are pure page-side JS writing a plain JS object, and
  the blocked C++ side polls that object with a QTimer (timers reliably
  resume the suspended loop). Never wait on a Qt async JS API from a nested
  loop.
- **External drops arrive through page-side glue, drained by a Qt timer.**
  Qt 6.8 wasm registered no browser dragover/drop listeners at all (only
  dragstart/dragleave/dragend for its own drags), so the browser never
  delivered a desktop file drop to Qt. The glue installs page-side
  dragover/drop listeners itself (`install_web_drop_target`, wired in the
  MainWindow constructor), reads the dropped files in JS into a plain JS
  queue, and a 250 ms QTimer on the C++ side drains that queue into
  `MainWindow::handle_web_file_drop`, one MEMFS path at a time. The desktop
  `QDropEvent` path stays untouched. The timer is not a convenience, it is
  the third platform finding (below): the glue used to call a wasm export
  directly from its Promise.all callback, which froze the deployed build the
  moment a drop happened while another document was open. Qt 6.10 also
  registers its own `dragover`/`drop` listeners on the window element, but
  its handlers run deferred (the JS listener only queues the event for the
  suspend-resume control), so they see a neutered `dataTransfer` for real
  drops, and its drag-move preview carries only `blob://placeholder` urls,
  which `accept_open_file_drag` rejects; Qt's native path is inert for
  external file drops and no double-open happens.
- **A raw JS-to-wasm export call must never lead to a nested event loop.**
  JS code (a promise callback, any hand-registered listener) must not call a
  wasm export whose C++ path can suspend: that entry runs outside Qt's
  suspend-resume control, and if the idle-suspended main loop already has a
  resume in flight (any live timer of an open document), the nested suspend
  clobbers the single-slot Asyncify state and the main thread's continuation
  is silently lost. The app parks forever with the "Opening..." dialog up, JS
  timers keep running, resizing leaves a white canvas, and the console shows
  nothing. This froze the deployed 0.86 site on any drop made while a
  document was open (2026-07-31): the drop glue used to call
  `_patchy_wasm_drops_ready()` straight from its Promise.all callback. A
  first drop on a fresh boot usually survived by timing luck, which is why
  quick local checks kept passing. Page-side JS writes plain state; the C++
  side polls it from a QTimer, whose handler the suspend-resume control runs
  inside the resumed main loop, where nesting an exec is safe. The picker's
  poll timer and the drop queue's drain timer are both this rule.

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
- **Non-modal dialogs are restacked above their parent window.** Qt's wasm
  compositor raises whichever window is clicked and does not re-raise that
  window's transient children with it, so a canvas click buried every open
  non-modal dialog (Layer Style, Curves, the Script Manager, filter dialogs)
  behind the fullscreen main window, with no window manager to recover it and,
  for the preview dialogs, an edit lock still engaged. `keep_dialog_above_
  parent_window` (dialog_utils.cpp) therefore has a wasm implementation beside
  the macOS one: it registers every dialog `run_non_modal_dialog` shows, and
  when a press or activation reaches a window that hosts registered dialogs it
  restacks their visible dialogs above it one event-loop turn later (after the
  compositor's own raise), walking the parent chain so a picker stays above the
  Layer Style dialog that stays above the main window. The earlier fix parked
  the main window in the compositor's stay-on-bottom zone with
  `Qt::WindowStaysOnBottomHint`; that is gone, because Qt 6.10 honours
  transient parents and placed a main-window-parented dialog (every script
  dialog) into the bottom zone too, where it opened invisibly underneath the
  window that spawned it.
- **Windows created inside a nested event loop used to be input-dead.** On the
  Qt 6.8 kit, a window whose platform window was constructed while the app was
  inside a reentrant `QEventLoop::exec` registered its DOM listeners through an
  embind call that handed back a Promise instead of the listener object, so the
  browser had nothing to call: every combo popup, context menu and sub-dialog
  opened from a dialog painted correctly and ignored all input (upstream
  QTBUG-145018). Patchy carried a large workaround layer for this: in-window
  combo choosers, an application-wide filter that reparented sub-dialogs into
  their host window as child widgets, and an in-window replacement for dialog
  context menus. All of it was deleted with the 6.10.3 upgrade, which fixes the
  defect: a window created inside a nested loop now registers ordinary function
  listeners (verified in the browser 2026-07-31 by censusing every
  `addEventListener` call). Dialog combos, sub-dialogs, and dialog context
  menus all use the normal Qt paths again. If a future kit regresses this, the
  symptom to look for is a Promise where a listener object belongs.
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
- **A window shown under an application-modal window never gets its keyboard
  back.** Qt marks every window created while an application-modal window is
  visible as blocked by it, and the key-delivery path drops events for a blocked
  window outright (`QGuiApplicationPrivate::processKeyEvent` only sends when
  `!blockedByModalWindow`, while the mouse path also checks the active popup, so
  a blocked window keeps receiving clicks). The desktop platforms clear the flag
  when the modal hides; the wasm plugin does not, so the window stays deaf for
  the rest of its life. The symptom is distinctive and misleading: the window
  paints, animates, and responds to the mouse, Qt's focus state is entirely
  correct (`focusWindow`, `activeWindow`, `focusWidget` and the window's focus
  object all point at it), Qt's own DOM `keydown` listener for that window still
  fires, and yet no key event reaches any receiver in the application - an
  app-wide `qApp` event filter sees nothing at all, and even Escape does not
  close the dialog.

  This is what made Breakout unplayable on the web build: it builds its
  document, layers and brick field first, spending well over the 0.5 s busy
  threshold before calling `patchy.ui.createCanvas`, so the app-modal script stop
  panel was on screen at the moment the controller window was created and that
  window came up permanently keyboard-dead (verified 2026-07-31). Pong opens its
  window as its first statement, before any work, so the panel had not appeared
  yet and it was never seen failing - which is exactly why the guard belongs in
  the host and not in the scripts: whether a game is playable must not depend on
  how much setup it happens to do first. Both games were re-verified playable in
  the browser after the fix (Breakout launches and steers, Pong's paddle tracks
  Up/Down). Two guards keep it from recurring, both in the script host:
  `createCanvas` calls `dismiss_busy_indicator()` before constructing the
  window, and `pump_progress_indicator` does not raise the stop panel while the run
  owns an open canvas window (`has_open_canvas_window`). The interactive helpers
  - alert, prompt, the pickers, showDialog, runCommand - already avoided the
  hazard through `ModalWatchdogPause`; `createCanvas` was the one script-owned
  UI surface that did not. Regression cover:
  `ui_script_canvas_window_dismisses_stop_panel` and
  `ui_script_canvas_window_suppresses_stop_panel` sample from inside the busy
  pump and fail if a canvas window and the panel are ever on screen together.

  The general rule for new code: never create a window while the app-modal busy
  panel can be up. If some future surface needs to, dismiss the panel first.
- **No `processEvents` pump runs mid-operation on wasm.** A pump cannot paint
  or deliver input here: the browser gets no turn until the main thread
  suspends in an idle event loop, and while wasm code runs, browser events only
  accumulate in Qt's pending-event queue. What a pump does do is dispatch Qt's
  own timers into the middle of the running operation. That is how a slow
  script froze the tab past recovery: the script busy-indicator pump
  (`pump_progress_indicator`, script_engine.cpp) dispatched a script canvas
  window's 16 ms frame timer while `evaluate()` was still on the stack, the
  frame handler pumped again, and the main thread never reached a suspend
  point. Both pumps are compiled out under `Q_OS_WASM` (the other is the
  processing-overlay tick in canvas_widget_render.cpp). The cost is that a long
  synchronous burst shows no progress until it yields, which is the honest
  behavior on this platform; long *filter* work is unaffected because it
  already runs on a worker thread (`run_filter_compute_with_progress`). Two
  companion guards belong to the same fix: `call_script_callback` refuses
  reentry while script code is already executing, and the script canvas frame
  timer is single-shot, re-armed after each frame, so a frame slower than its
  interval cannot leave a permanently-expired timer in the queue.

## Release deployment (rtsoft.com/patchy)

The web build is part of the standard release flow; the batch-file details live
in [release-process.md](release-process.md). Short version:
`scripts\release\build-wasm.bat` (run by `release-all.bat`) builds the
`wasm-release` preset and stages the deployable files into
`build\package\wasm-site` (no separate worker file: Emscripten 3.1.58+ folds
the pthread bootstrap into `patchy.js`, where the 3.1.56 builds emitted
`patchy.worker.js` alongside it);
`scripts\release\start-local-wasm-server.bat` serves that staged payload for
a browser check (it stops a server left over from a previous run, then opens
the site in the default browser), while
`scripts\release\start-local-wasm-test-server.bat` serves the raw
`build\wasm-release` directory for the development loop;
`scripts\release\upload-wasm-to-rtsoft.bat`
(run by `upload-to-rtsoft.bat`) publishes it to `rtsoft.com/patchy` over
ssh/scp.

**Cross-origin isolation is a hard serving requirement.** The multithreaded
build needs SharedArrayBuffer, which browsers only enable when the document
arrives with `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`. Three layers keep that true:
the staged `.htaccess` sets both headers (`Header always set`, inside the
existing IfModule guard), `scripts\wasm\serve.mjs` sends them locally so the
dev loop and the staged-site check match production, and the shell page
checks `window.crossOriginIsolated` before fetching the wasm and shows a
clear error naming the two headers instead of the inscrutable
SharedArrayBuffer failure the loader would otherwise hit. Because the
IfModule guard would silently drop the headers on a host without
`mod_headers`, `upload-wasm-to-rtsoft.bat` curls the live site after every
upload and fails loudly if either header is missing (rtsoft.com serves them
today; verified 2026-07-30). Every page asset is same-origin, so
`require-corp` needs no per-asset CORP headers, and the page's
`target="_blank" rel="noopener"` outbound link is unaffected by COOP.

The deployed page is not Qt's generated `patchy.html` shell (that one still
lands in `build\wasm-release` and serves the dev loop via `serve-app.ps1`).
The site stages a Patchy-branded shell configured from
`packaging\web\patchy.html.in`: logo, app name and version, and a real
download progress bar. The browser-tab favicon is the app's own multi-size
icon: `build-wasm.bat` stages `src\app\patchy.ico` as `favicon.ico`, the
shell links it with the cache tag, and `patchy-logo.png` doubles as the
`apple-touch-icon`. Emscripten exposes no download-progress callback, so
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

## Multithreading migration (step 4a, 2026-07)

The switch from `wasm_singlethread` to `wasm_multithread` landed with these
pieces (rationale in the decision bullets above):

- Kit and preset: `setup-qt-wasm.ps1` installs `wasm_multithread`, the
  `wasm-release` preset points at its toolchain and compiles with
  `-fexceptions -pthread`, and the app link adds `-pthread`,
  `-Wno-pthreads-mem-growth` (the same one-diagnostic suppression wasm-core
  carries; pthreads plus a growable heap slows JS-side heap views, which Qt
  touches only for canvas presentation), and the 4 MB worker stacks.
- Threading seams re-gated on `!defined(__EMSCRIPTEN_PTHREADS__)`, the CMYK
  worker cap, and the pool expression, as described above.
- The single-threaded paint inversion is also fixed for any future
  single-threaded build: `should_defer_full_refresh_to_async` and
  `should_defer_first_render_to_async` (canvas_widget_render.cpp) return
  false when `kBackgroundWorkRunsInline` (background_workers.hpp), because
  deferring to an inline worker composed the frame inside `paintEvent`
  anyway, plus a Document deep copy and a second repaint. On the
  multithreaded build the predicate is false and the deferred path works as
  designed.
- Verified 2026-07-30 in Chromium behind the COOP/COEP `serve.mjs`:
  zero-warning build, `patchy.worker.js` emitted and staged,
  `crossOriginIsolated` true, pool workers spawn, boot to painted UI in
  under a second on a warm cache, the New Document dialog opens and closes
  into a nested exec loop with threads live, menu mnemonics track, the
  Script Manager opens, reloads are clean, and the full `patchy_core_tests`
  suite (including the three byte-stability canaries) passes under node with
  the 16-worker CMYK cap. Sizes: `patchy.wasm` 79,734,153 bytes (76.0 MiB,
  +8.6% over single-threaded for the pthread instrumentation), `patchy.js`
  395,340, `patchy.worker.js` 2,839, `patchy.data` unchanged.
- Long filter and adjustment commits no longer freeze the tab.
  `run_filter_compute_with_progress` (main_window_shared) runs the commit
  compute on a worker on threaded wasm while the main thread waits in an
  event loop, so the progress dialog paints, animates, and can cancel; a
  main-thread compute pumped with processEvents never returns control to the
  browser, which is why the old path presented no frames and got the tab
  flagged unresponsive. Desktop keeps the historical on-thread compute with
  the dialog driven from the progress callback. All five commit sites go
  through it: destructive filter, Filter Gallery, Liquify, Levels, Curves.
- Slow live previews show a canvas badge on every platform: while a preview
  worker is in flight past the standard overlay delay the canvas paints a
  "Rendering preview..." spinner (see docs/filters.md), which matters most
  on wasm where renders run a few times slower than native.
- Still owed from the interactive battery (needs a visible browser pane):
  preview scrub with live cancellation and the processing spinner, file
  picker and drag-drop under threads, a corrupt-PSD worker exception, RAW
  develop on the 4 MB worker stack, a CMYK PSD over 4 Mpx, a big-document
  invalidation repaint, the pool=2 lazy-spawn probe, the stuck-script
  watchdog interrupt, and before/after benchmark numbers (the
  `?PATCHY_RENDER_SINGLE_THREADED=1` control plus `build\wasm-st-baseline`,
  a preserved single-threaded build, exist for exactly that comparison).

## Kit upgrade to Qt 6.10.3 + emsdk 4.0.7 (2026-07-31)

The kit moved from Qt 6.8.3 / Emscripten 3.1.56 to Qt 6.10.3 / Emscripten
4.0.7 to fix the reentrant-`exec` window defect at the source rather than
keep working around it. What the upgrade changed, in order of how much it
cost to find:

- **The whole dialog input workaround layer is gone** (see the browser UI fit
  section): combo choosers, sub-dialog embedding, in-window dialog context
  menus, and their `exec_context_menu` funnel. Windows created inside a nested
  loop take input normally now. This was the point of the upgrade.
- **`-sASYNCIFY_STACK_SIZE=1048576` had to be added**, or the app hard-crashes
  the tab the first time a dialog opens a dialog. Details in the decisions
  section; this is the one change nobody would predict from the changelogs.
- **`patchy.worker.js` no longer exists**; the staging and upload file lists
  lost it.
- **No `-sASYNCIFY_IMPORTS`**: 6.8 shipped its own `qt_asyncify_suspend_js` /
  `qt_asyncify_resume_js` imports and set that flag from its cmake; 6.10
  suspends through `EM_ASYNC_JS` instead and sets nothing.
- **The main window no longer needs `Qt::WindowStaysOnBottomHint`** and must
  not have it (QTBUG-131699's transient-parent stacking fix landed in 6.10,
  which turned that hint into a trap for main-window-parented dialogs).
  Dialog stacking is handled by the wasm `keep_dialog_above_parent_window`.
- **Qt now registers its own drag/drop listeners** on the window element.
- Sizes: `patchy.wasm` 81,986,408 -> 83,013,429 bytes (+1.3%), `patchy.js`
  395,340 -> 357,451. Full `patchy_core_tests` under node: 736 pass, 0 fail,
  with the three byte-stability canaries byte-identical on the new toolchain,
  which makes wasm/musl-on-4.0.7 the fourth toolchain to reproduce the pinned
  bytes.

Upstream references behind the decision, researched 2026-07-30: QTBUG-145018
(our defect from the other end: a submenu that paints but takes no input,
console "Property 'handleEvent' is not callable"; Qt blames reentrant
`QEventLoop::exec` on wasm between 6.8.0 and 6.10.0 and the reporter confirms
6.10.2+ is clean), QTBUG-102827 (Asyncify plus nested exec loops, fixed in
6.10), and QTBUG-131699 (non-modal dialog invisible behind its parent, fixed
in 6.10.0 Beta3 via `e48c19449e`). Qt 6.11 carries further plugin work that
may matter later: `01d48cd` "wasm: process events targeted at the window
only", `5396a9e` "wasm: Better handling of transient parent", `52c5a78`
"wasm: fix QWasmWindow child element layout", and `d8112c7` (ResizeObserver
does not fire for a `display: none` container). Nothing upstream matches the
per-window devicePixelRatio/canvas-scale mismatch seen when the browser DPR
changes after a window is sized; no wasm-plugin commit has touched
`devicePixelRatio` since 2022. Moving to 6.11 is blocked on aqtinstall, not
on Qt (see the provisioning section).

## Later steps (not built yet)

- Step 4 remainder: memory tuning (per-platform undo cap and byte budget,
  tile-cache eviction), texture lazy-fetch and compressed
  packaging/deployment, the measured document-size cap the web build
  advertises, and preset/library persistence (IDBFS or OPFS) so user presets
  survive reloads.
