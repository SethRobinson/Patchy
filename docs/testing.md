# Testing and visual QA

Read this before adding tests, changing test infrastructure, diagnosing suite-only failures, or driving Patchy for visual verification.

## Suite organization

The core suite is one binary split across `tests/core/*_tests.cpp`, one TU per thematic group. Each TU ends with a `<group>_tests()` registration function; `tests/core/main.cpp` concatenates them in a fixed order. Never use static self-registration because cross-TU initialization order would reorder the suite. Append tests to the correct group registration vector. Shared Qt-free helpers live in `tests/core/core_test_support.{hpp,cpp}` and `psd_test_support.{hpp,cpp}` under `namespace patchy::test`; move shared helpers there rather than copying them.

The UI suite follows the same design in `tests/ui/*_tests.cpp`. Registrations are declared in `tests/ui/ui_test_groups.hpp` and concatenated by `tests/ui/main.cpp` in a load-bearing order: contact-sheet and README tests consume artifacts written earlier, and QSettings state intentionally crosses tests. Shared helpers live in `tests/ui/ui_test_support.{hpp,cpp}` under `namespace patchy::test::ui`. `MainWindowTestAccess` in `tests/ui/ui_test_access.hpp` is befriended by its qualified name in `main_window.hpp`.

Unicode and special-character path tests (`unicode_path_tests` in both suites, plus `ui_script_io_round_trips_unicode_path`) share their file names through `tests/unicode_path_names.hpp`. Spell non-ASCII in those names and in any Qt-free TU as `\u`/`\U` escapes inside `u8""` literals: `patchy_core_tests` compiles without `-utf-8` (only Qt-linked targets inherit it), so raw UTF-8 bytes would be read through the ANSI code page and raise C4819.

Groups that outgrew ~3,000 lines are split into part files (`<group>_tests_<theme>.cpp`, each exporting `<group>_tests_partN()`); the original `<group>_tests.cpp` stays as a small aggregator whose exported function concatenates the parts in the original registration order, so the suite order is unchanged. Add a new test to the correct part file's registration vector, keeping the group's overall order intact. Helpers shared by two or more parts of one group live in that group's `<group>_test_support.{hpp,cpp}` (moved, never copied); helpers used by one part stay in that part's anonymous namespace. `patchy_core_tests` has no `/bigobj`, so core part files must stay under ~3,000 lines.

Local-fixture tests skip on a remote machine until `local-test-fixtures` is copied there, because that directory is deliberately untracked. Sync it from the repo root with `tar -cf - local-test-fixtures | ssh <host> 'tar -xf - -C ~/patchy/src'` (Git Bash; macOS tar drops four `__MACOSX/._*` AppleDouble entries, which are not test inputs). The snapshot checkout leaves untracked files alone, so one sync persists across later `remote-build.ps1` runs. Read the per-platform consequences in [platform.md](platform.md) before doing this: a synced corpus turns previously skipped text tests into failures and one hang. The repository-wide fixture sourcing rule lives in `AGENTS.md`.

The composite corpus digest baselines live next to the PSDs they pin. The committed
fixtures' baselines, `test-fixtures/psd/flatten-digests.txt` and `render-digests.txt`,
are tracked, so every machine (remote snapshots included) checks the same bytes and a
re-pin is a reviewable diff. To add a committed PSD or re-pin after a deliberate
rendering change, delete the tracked file, rerun `composite_corpus` in the core and UI
suites, review `git diff`, and commit it; a failing run also writes the actual digests
under `test-artifacts/composite-corpus/` so a re-pin can be a copy. Never re-pin per
machine. `local-test-fixtures/composite-corpus/*-digests.txt` is the untracked overlay
for documents dropped into that directory only; a snapshot sync never copies it.

## Running and filtering

Run `patchy_ui_visual_tests.exe` with `QT_QPA_PLATFORM=offscreen`. The harness forces offscreen itself; `PATCHY_UI_TEST_PLATFORM=<qpa plugin>` (`cocoa`, `windows`, `xcb`) runs it on a real platform instead, which is the only way to reach native menubar and window-activation code (the issue 29 macOS crash; add `NSZombieEnabled=YES` there so a message to a freed Cocoa object aborts instead of passing silently). Screens and fonts differ there, so run a filter, never the whole suite. Both release test binaries accept a name substring as their first argument. The UI suite also reads `PATCHY_UI_TEST_FILTER`; there is no `--test` flag. The UI filter may also be a comma-separated list of substrings (a test runs if its name contains any of them), which is how to reproduce ordered cross-test interactions: select the state-leaking test and its victim in one run. The core suite takes a single substring only.

Never run two test processes (or a test process and the app) at the same time: they share the QSettings store, and a concurrent run rewrites preference keys mid-test, producing failures such as `ui_language_saved_preference_overrides_system_language` seeing its saved language clobbered. Run suites sequentially.

The QSettings store also persists across runs, and a killed run skips every customize-then-restore test's restore step. Any settings group that one test customizes while another test asserts its defaults without seeding them (the `hotkeys` group is the known case) must be removed by the bootstrap block in `tests/ui/main.cpp`; groups whose assertion sites all clear or seed their own keys first (`palettes`, `colorPanel`, `saveOptions`, `newDocument`, `recentFiles`) need no bootstrap entry.

Tests save PNG artifacts through `save_widget_artifact(...)` into `test-artifacts/` beside the binary. Inspect them directly when verifying rendering. Renaming an artifact also requires updating the contact-sheet list in `tests/ui/readme_screenshot_tests_classic.cpp` (the readme_screenshot_tests group is split into part files behind an order-preserving aggregator); stale files in long-lived build directories can otherwise hide the mismatch.

## Offscreen fonts and input

**The offscreen platform on Windows rasterizes text through FreeType; a real window uses DirectWrite.** The two disagree: DirectWrite applies a QFont stretch to the advances only and draws every glyph at its design width (issue 20's 103% "M" came out 3% narrow on screen while every offscreen pin passed). Text rendering that must match on screen is drawn engine-independently (`draw_line_glyphs_pixel_aligned`, docs/text-render-calibration.md) and its DirectWrite acceptance is a fresh GUI instance of the release build, `PATCHY_NO_SINGLE_INSTANCE=1` plus `--run-script` without `--headless` (a window appears), exporting the layer for a pixel diff; the same run with `-platform windows:fontengine=freetype` reproduces the suite's engine.

The offscreen platform does not enumerate installed Windows fonts. Register required faces through `tests/test_fonts.hpp` or `QFontDatabase::addApplicationFont`. Never remove an application font during the suite because invalidating an in-use font cache can crash it. The Windows registry font rescue (`try_register_missing_system_font_family`) is switched off under offscreen unless `PATCHY_HEADLESS` is set, which only a `--headless` app run does. A test that names a family must also work on the Linux host, which has no Arial, Segoe UI, Calibri or Verdana: include `Liberation Sans` / `DejaVu Sans` (the Linux `UiDefault` role) in its candidates, since a text edit session moves a layer with a missing family onto its substitute (`substituted_text_family`).

Because fonts are never removed, every registration is permanent suite state: newly present families change which face Qt's missing-family fallback picks, which moves text metrics in every later test (the PSD text re-edit tests in `text_transform_commit_tests` pin committed rasters against that fallback and fail if a mass registration runs first). Register only the faces a test actually needs. A test that must register a large inventory runs it in a child process instead: `ui_bundled_web_fonts_register_and_create_engines` spawns `patchy_ui_visual_tests.exe --bundled-web-fonts-probe` (handled in `tests/ui/main.cpp` before the QSettings bootstrap, so the child never touches the parent's settings store).

FreeType may expose an OpenType typographic family rather than its familiar GDI family, such as Arial with style Black for `ariblk.ttf`. Use `available_text_family_style_match`; do not gate tests on `QFontDatabase::families().contains(...)`.

A writing system that no registered face covers (Thai or Japanese text in the suite or under `--headless`) makes Qt resolve the request to its glyph-box engine, whose family list is empty; `QRawFont::familyName()` indexes that list unchecked and crashes. Check `QFontDatabase::families(system)` before asking a per-script `QRawFont` for its family (`text_family_draws_any_of` in main_window.cpp is the reference; `ui_script_text_layer_with_uncovered_script_does_not_crash` pins it).

Offscreen does not clear `QApplication::keyboardModifiers()` after synthetic key events, and the stuck bit persists in the shared QApplication. Assert behavior through code that reads the current event's folded modifiers. `ui_brush_alt_shows_eyedropper_cursor` is the order-independent reference.

## Failure and lifetime traps

- Timer-driven checks wait for observable state with a bounded deadline. A fixed
  sleep does not guarantee a number of timer deliveries on a busy machine. The
  Airbrush opacity-cap test retains its pixel bounds and PSD round trip while
  allowing delayed timer delivery.
- A wall-clock limit in a test is a hang guard, never a performance bound: give it
  minutes (`kRawDialogDeadlineMs` in `camera_raw_heif_tests.cpp` is 120 s), because a
  concurrent build or another suite on the machine must not turn it into a failure.
  A fixed sleep is never a synchronization primitive; wait for the state itself
  (`process_events_until`, or the marker file a script writes, as `protocol_edges` in
  `tests/mcp_client_tests.py` does before it cancels).
- A debounced worker's start is not a fixed offset from the trigger. To prove work runs
  off the event loop, park the worker, wait for it to start, and check that a UI timer
  fires while it is still parked
  (`ui_filter_gallery_heavy_thumbnail_queue_yields_to_event_loop`).

- The test `CHECK()` macro throws. A failure while a MainWindow still owns an open inline text editor can abort during unwind without printing a `[FAIL]` line. Commit or close the editor before assertions that may throw.
- The test binaries can exit 0 even when tests fail. Never trust the exit code alone; grep the output for `[FAIL]` to judge a run. Both runners print `[PASS]` on stdout and `[FAIL]` on stderr, so when a run is captured to files, grep the stderr capture (a stdout-only grep reports zero failures for any run).
- Never let a driver lambda (a `QTimer::singleShot` body or any slot) throw across Qt event dispatch; Qt does not support it, and on macOS the suite aborts in the CFRunLoop frames. Wrap the driver body in try/catch and pass `std::current_exception()` to `patchy::ui::unwind_non_modal_dialog_loop` when the code under test is parked in `run_non_modal_dialog`. `ui_filter_gallery_unwinding_call_disarms_in_flight_renders` is the reference.
- Clicking a layer-row content or mask thumbnail may rebuild and delete the row widget between press and release. Use `click_layer_row_thumbnail(...)`, which refetches the widget for both events; never retain the old pointer.
- If the UI suite dies with an access violation, read the symbolized stack appended by the dbghelp vectored handler in `tests/ui/main.cpp`.
- A crash that occurs only in the full ordered suite is usually an order-dependent heap error. Use the `linux-asan` procedure in [platform.md](platform.md); never reorder or skip tests to conceal it.
- Tests that enable `imports/showPsdWarningsAndInfo` need a repeating QTimer notice dismisser. A one-shot can fire during open progress and leave the suite hung; see [file-formats.md](file-formats.md) under Import notices.
- Platform-specific skips and their reasons are maintained in [platform.md](platform.md).

## README screenshots

`scripts\make-readme-screenshots.ps1` regenerates `docs/images/screenshots/`. Two pipelines:

The current showcase, per-image inputs, and regeneration commands are listed in
[readme-showcase.md](readme-showcase.md). KPT is an explicit driver scene
(`-Scene plugin_kpt5`, or `-IncludeLegacyPlugins` for a full run) using an isolated
portable host and fixture copy. It combines direct Patchy and plug-in window
captures; neither source is a desktop screenshot.

- **Script-driven scenes** (`scripts/dev/readme-shots/*.js`, listed in the driver's
  `$jsScenes` table): a fresh unattended `patchy.exe --run-script` run stages the UI with the
  `patchy.ui` staging APIs (setWindowSize/setSidePanelWidth/captureWindow/setStatusMessage,
  plus the activeLayer panel reveal) on the REAL windows platform, so every installed font
  renders. Offscreen enumerates no installed fonts, so scenes needing unregistered
  installed faces use this pipeline. The driver pins DPI (`QT_ENABLE_HIGHDPI_SCALING=0`, `QT_FONT_DPI=96`),
  sets `PATCHY_NO_SINGLE_INSTANCE=1`, and isolates settings with `PATCHY_SETTINGS_DIR` (an
  app-level env hook in src/app/main.cpp that redirects the ini-backed `app_settings()` store)
  so a run never touches the user's real Patchy state. The app window appears on screen for a
  few seconds per scene.
- **Offscreen test scenes** (everything not yet migrated): the `shot_readme_*` scenes in
  `patchy_ui_visual_tests` run offscreen and their artifacts are copied out. New scenes should
  be authored as scripts in the first pipeline; the remaining test scenes migrate over time and
  stay in the suite as regression smoke tests either way (the driver skips copying a test
  artifact whose scene has a script-driven owner). Scenes that picture a modal dialog the
  scripting surface cannot open stay here permanently: `shot_readme_image_trace` captures the
  Trace Image to Shapes dialog, which `layer.traceToShapes` bypasses and `app.runCommand`
  would block a script on; `shot_readme_long_shadow` opens the Layer Style dialog
  beside generated editable text with a live continuous shadow.

One scene is invoked separately: `scripts/dev/readme-shots/plugin-dialog.js` produces
`docs/images/screenshots/plugin_dialog.png`, the dialog of a third-party 8bf plug-in
(Mehdi's Absolute Color from `local-test-fixtures/photoshop-plugins/mehdi`) captured through
`layer.applyPlugin(id, {captureDialog})` on Windows. It is not in the driver's table because
the plug-in is not committed; invoke the script explicitly (see its header) when the shot
needs refreshing. Its window-targeted capture and automatic dialog answers are authorized
Patchy automation, not desktop control.

Both pipelines round the corners of the window they captured, because DWM rounds Patchy's
frameless windows in the compositor and a `QWidget::grab()` is therefore square. The offscreen
side does it in `save_readme_shot` and `draw_readme_overlay` (which also rounds the shadow it
fakes under a composited dialog); the driver does it to the script-driven PNGs in
`Set-RoundedWindowCorners`. Both use 8 px, `DWMWCP_ROUND`'s radius at 96 DPI, and leave the
corner pixels transparent so a shot reads as a window on a light or dark page. Change the
radius in both places or the two pipelines drift.

## Native visual QA and app-driving commands

Automation confined to Patchy is authorized, including scripted clicks, typing, and
captures of Patchy windows and its hosted plug-in windows, such as KPT. No additional
permission is needed. This does not authorize desktop screenshots, global mouse or
keyboard input, interaction with other applications, or taking control of Seth's
desktop. Captures must come directly from the intended application windows, not from
a desktop screenshot cropped afterward. Adobe Photoshop COM remains authorized for
capture, verification, and acceptance as specified in AGENTS.md.

Use Patchy's scripting, CLI, MCP, or messages targeted to its own widgets/windows.
`patchy.ui.captureWindow`, `QWidget::grab`, and the plug-in helper's `PrintWindow`
capture only the intended windows. Targeted clicks and typing in the helper's KPT
window are permitted; global cursor movement, `SendInput`, and desktop-wide Computer
Use still require explicit authorization in the current request. An MCP wrapper does
not make those desktop actions authorized.

Keep unattended runs in task-owned instances with isolated settings. Prefer offscreen
work where possible; when native windows are needed, keep them bounded and avoid
disrupting other applications. Permission for Patchy input does not authorize discarding
unsaved work, closing the user's running app/connector, or stopping unrelated processes.

`patchy-mcp --attach` connects to an already-running interactive Patchy; use it
only when authorized to control that workspace. For automated attachment tests,
launch a test-owned app with `QT_QPA_PLATFORM=offscreen`, isolated
`PATCHY_SETTINGS_DIR`, and a unique `PATCHY_MCP_ENDPOINT` shared with the proxy.
Do not pass `--headless`: it deliberately disables attachment. `ui_mcp` covers
state guards, input locking, cancellation, reconnect, and unsaved history.

For persistent background editing use `patchy-mcp`, which owns an isolated
offscreen workspace. When a task needs its own visible Patchy workspace,
`patchy-mcp --visible` runs the same protocol visibly under the rules above.
`patchy-mcp --check` validates native strokes, previews, and
the assembled control kit from its installed location. See [ai-control.md](ai-control.md).
The UI filter `ui_script_automation` covers native stroke parity, pressure,
selection, palette snapping, history, stale IDs, and Unicode preview output.

The standard-client integration test uses a development-only Python 3.10+
environment:

```powershell
python -m venv .deps/mcp-client
.deps/mcp-client/Scripts/python -m pip install 'mcp>=1.26,<2'
cmd /s /c 'scripts\run-throttled.bat .deps\mcp-client\Scripts\python.exe tests\mcp_client_tests.py build\release\patchy-mcp.exe'
```

Run from the repository root. Artifacts stay under `test-artifacts/mcp`. The test
uses only owned offscreen processes and also accepts a connector in a staged
package directory, exercising resource discovery without source-relative paths.
Python is not required by the shipped connector.
On the mac build host, use `.deps/mcp-client-py312/bin/python` (project-local Python 3.12);
its system Python 3.9 cannot install the MCP dependency. On the linux build host, use
`.deps/mcp-client/bin/python`. Run from the remote repository root with
`nice -n 10 <python> tests/mcp_client_tests.py <connector>`.
The full suite includes both owned and attached workspaces, competing clients,
cancellation, disconnect cleanup, app restart, stale tokens, and shared history.
Unix crash simulations remove their own socket paths during cleanup.
Pass `--attachment-recovery-only` after the connector path to reproduce offline
discovery, late app startup, restart with fresh state tokens, and interruption
without replay. This test owns its offscreen apps and requires no prior artifacts.
The client suite also passes `--visible` with an explicit offscreen Qt backend
to verify option handling and truthful mode/preview metadata without opening a
desktop window. A real visible smoke test may use a task-owned Patchy instance under
the rules above without separate permission for Patchy-only input or captures.
On Linux it also holds a fake desktop bus open without answering and verifies that
headless scripting and an immediately closed MCP client still exit promptly.
After installing the current Flatpak user bundle, run `nice -n 10 .deps/mcp-client/bin/python
tests/flatpak_mcp_tests.py` from the host. This launches the app and connector in
separate sandboxes with the default endpoint, verifies an owned document before
editing, and checks preview and unsaved reconnect. Close any existing Patchy Flatpak first; the test refuses
to run alongside it and removes only its own processes and runtime socket.
The host session must set `XDG_RUNTIME_DIR`; the test forwards it through the MCP
SDK's filtered subprocess environment so both launchers use the same runtime mount.
Pass `--recent-history-only` after the connector path to check shared history
with owned headless and MCP processes. The UI filters `ui_unicode_recent_history`
and `ui_vector_preview_action_persistence` cover history merging/live refresh and
the restored preview preference's menu/Preferences synchronization.

Use Patchy's command-line control surfaces and inspect their window captures directly;
the permission boundary above applies to native QA as well as screenshot generation.

`patchy.exe --screenshot <out.png>` captures the running instance without raising or focusing it. Add `--screenshot-widget <qtObjectName>` and/or `--screenshot-rect x,y,w,h` to narrow the capture, and combine it with positional files to open a document. The invoking process exits immediately, so poll for the output. If no instance is running, Patchy opens, waits about 1.5 seconds, captures, and exits with code 0 on success or 3 on failure. Add `--language <code>` (with `--headless`, so no running instance is reused) to capture a specific UI language without changing the saved preference; see [localization.md](localization.md). Never run `patchy.exe --help` or `--version` during verification: the Windows GUI build has no console, so Qt shows them in a message box that pops over whatever Seth is doing.

`patchy.exe --stress-test[=quick|small|standard|huge] [--stress-report-dir <dir>]` builds the deterministic performance scene and exits. Reports default to `%APPDATA%\Patchy\stress-reports\`; read `stress-latest.json`. Use quick at 1024 px for iteration and standard at 4096 px for full-scale measurements. Meaningful timings require a real screen. See [performance.md](performance.md).

`patchy.exe --headless ...` runs any of those modes with no display (Qt's offscreen platform). It never forwards to a running instance, so the exit code and the `--script-output` file belong to the run itself; prompts are suppressed and sound is muted. Prefer it for unattended `--run-script` runs from tooling and agents. Leave it off when a capture must show the real platform and its installed fonts (the offscreen platform sees only bundled fonts; on Windows a headless run loads the installed fonts from the registry on the first text request instead).

Useful diagnostic variables:

- `PATCHY_NO_SINGLE_INSTANCE=1` allows multiple instances.
- `patchy_core_tests` also validates the hardware-agnostic GPU foundation: dirty-region coalescing, tile invalidation by mip, deterministic render-graph ordering, cycle and multiple-writer rejection, explicit RGB8 comparison tolerances, CPU-equivalent tiled rendering, bounded dirty-tile updates, and fake-backend device-loss recovery. These tests do not open a window, download Dawn, or require a graphics adapter; see [Hardware-agnostic GPU render graph](gpu-render-graph.md).
- The optional UI build also compiles `WebGpuRenderBackend` when Dawn is absent. With a real Dawn prefix, an explicit `PATCHY_RENDER_BACKEND=webgpu` or `auto` run exercises the graph gate followed by Dawn's per-tile compute and regional readback path. Confirm `Patchy WebGPU document compositor active on ... via ...` and inspect the reported `tiles:` and `readback bytes:` values before treating the real backend as exercised. A missing package, failed graph submission, queue error, tile readback error, or recovery failure is expected to fall back without changing CPU-authoritative tests.
- `PATCHY_FAKE_SCANNER_FILE=<path>` bypasses native scanner acquisition in tests.
- `PATCHY_REV_TRACE=1` logs revision bumps.
- `PATCHY_ZOOM_TRACE=1` logs paint and zoom phases over 2 ms.
- `PATCHY_STYLE_MASK_CACHE_OFF=1` disables the style-mask cache.
- `PATCHY_RENDER_SINGLE_THREADED=1` forces byte-stable sequential rendering.
- `PATCHY_RENDER_THREADS=<n>` caps every parallel fan-out at n workers in place of the hardware thread count (perf harness emulation of a low-core machine; the transform proxy gate scales with it).
- `PATCHY_PROCESSING_OVERLAY_MIN_PIXELS` overrides the processing-overlay threshold.
- `PATCHY_RENDER_BACKEND=auto|webgpu|cpu|opengl|vulkan|metal|d3d11|d3d12` selects the graphics preference. `auto` attempts the optional Dawn/WebGPU document compositor when it was found at configure time and then uses Qt Quick/RHI for presentation; `webgpu` makes that preference explicit. `PATCHY_GPU_CANVAS=auto|cpu` remains an alias. A hardware scene graph or Dawn adapter can still fall back to the complete CPU compositor when the document is outside the GPU capability matrix, including malformed Blend If payloads.
- The CTest registration for `patchy_ui_visual_tests` sets `QT_QPA_PLATFORM=offscreen;PATCHY_RENDER_BACKEND=cpu;PATCHY_NO_SOUND=1;PATCHY_RENDER_SINGLE_THREADED=1;PATCHY_RENDER_THREADS=1;LC_ALL=C.UTF-8;LANG=C.UTF-8;LANGUAGE=C;QT_SCALE_FACTOR=1`. This keeps byte-sensitive UI/export tests CPU-authoritative, deterministic, and independent of Dawn, worker count, locale, and desktop scale. Exercise WebGPU separately with an explicit `PATCHY_RENDER_BACKEND=webgpu` or `auto` run.
- `PATCHY_BUILD_WEBGPU_VALIDATION_TESTS=ON` adds the manual `patchy_webgpu_equivalence_tests` executable. It is not a CTest entry and does not change the deterministic environment. With Dawn and hardware available, it compares native frames against `Compositor::flatten_rgb8()`, validates 255/256/257 pixel tile boundaries, supported masks and Blend If, checks incremental dirty-tile readback, and prints `[METRIC]` records for cold/warm full frames, one or multiple dirty tiles, and idle frames. Those records measure uploads, readback bytes, resource reuse, and CPU/Dawn elapsed time; they are not a product-level speedup claim. Without Dawn or hardware it reports `[SKIP]` rather than making the standard suite machine-dependent.
- The same manual executable runs `webgpu_device_loss_recovery_rebuilds_resources` when Dawn and hardware are available. It injects one controlled failure before queue submission and another before regional readback, verifies that a failed frame does not replace the previous image, recreates the Dawn device-owned caches through `recover()`, and compares the recovered full and incremental frames with the CPU compositor. Set `PATCHY_WEBGPU_INJECT_DEVICE_LOSS=before-submit` or `before-readback` only for a manual diagnostic; each stage is consumed once, and the standard CTest suite never sets either variable.
- `zero_copy_interop` validates only the Qt-free eligibility gate for a future native presentation bridge. It checks matching APIs, shared-device ownership, native texture import, and explicit synchronization. It must not be described as a zero-copy smoke test: the current Dawn path still performs regional readback and Qt Quick still presents a `QImage`.
- `PATCHY_ENABLE_VULKAN_QT_INTEROP_PROBE=ON` adds an opt-in native-window diagnostic that reads Qt Vulkan handles on the scene-graph render thread and observes Dawn's Vulkan instance and adapter identity when the pinned native target is available. Set `PATCHY_VULKAN_QT_INTEROP_PROBE=1` and `QSG_RHI_BACKEND=vulkan` to run it. A matching vendor/device identity only confirms the same physical GPU; it does not prove shared `VkDevice` ownership. The probe never imports or publishes a texture and must report zero-copy as disabled until shared device ownership, native texture import, and explicit synchronization are all verified. It is not a CTest entry.
- The same records expose queue submissions and waits. A non-empty full or dirty plan should report one of each, including `dirty_multiple`; an idle frame should report zero for both.
- `PATCHY_ENABLE_WEBGPU=ON` enables CMake discovery of an installed Dawn package; a missing package is expected and leaves the same binary on the Qt RHI/CPU path. `scripts/build-gpu.sh linux-release` performs the configure, build, core tests, and the deterministic backend-selection test in one step; set `PATCHY_QT_PREFIX` and `PATCHY_DAWN_PREFIX` when the dependencies are outside the preset. `scripts/build-dawn.sh --print-config` checks the pinned lock without network access, while `scripts/build-gpu.sh linux-release --dawn-only` builds and verifies the optional Dawn prefix. For a real Dawn build, use `--with-dawn`, inspect the configure output for the Dawn target, then run `PATCHY_NO_SINGLE_INSTANCE=1 PATCHY_RENDER_BACKEND=auto QSG_INFO=1 ./build/linux-release/patchy` with a real native display. Confirm a line containing `Patchy WebGPU document compositor active on ... via ...` before treating WebGPU composition as exercised. See [GPU build and dependency guide](gpu-build.md) and [GPU troubleshooting](gpu-troubleshooting.md) for the full setup and fallback procedure.
- `PATCHY_NO_SOUND=1` suppresses script audio; offscreen suites rely on it.
- `PATCHY_UPDATE_MANIFEST_URL=<url>` replaces the startup update check's manifest URL (a loopback server in the update-check tests). Pointing it at an unresolvable host such as `https://x.local/m.json` holds a resolver call open for seconds, which is how the issue 48 quit freeze was reproduced on macOS: quit within that window and time the exit (see the AGENTS.md rule on pending host lookups).
- `PATCHY_SETTINGS_DIR=<dir>` redirects the app's ini settings store (automation isolation).
- `PATCHY_RECOVERY_DIR=<dir>` redirects the automatic document recovery store (the UI suite sets it to `test-artifacts/recovery`); `PATCHY_RECOVERY_INTERVAL_MS=<ms>` overrides the recovery timer interval. See [document-recovery.md](document-recovery.md).
- `PATCHY_HEADLESS=1` marks a `--headless` app run (main.cpp sets it). Never set it for the suites: it re-enables the registry font rescue and makes layouts machine-dependent.
- `QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES=1` makes `patchy.exe --help` and command-line parse errors print to the redirected stdout/stderr instead of a modal Win32 message box (patchy.exe is a GUI-subsystem binary), which is what an automated `--help` check needs.
- `PATCHY_UI_TEST_FILTER` selects a UI test substring.
- `PATCHY_UI_PROFILE=1` prints stderr timing lines for instrumented UI stages (layer-panel rebuild phases, layer-style dialog open/close, undo snapshots).
- `PATCHY_PERF_SAMPLER=1` (patchy_perf_tests only) samples the main thread's stacks every 10 ms and prints the hottest ones at exit.

Composite checksums from stress reports or large renders are comparable only on the same machine: text antialiasing varies by system and the parallel strip renderer varies with thread count.

Committed PSD corpus fixtures must decode; unreadable committed files fail the
corpus test. Optional local files may still skip. The real-photo HEIC sweep skips
only a recognized unavailable-codec/backend condition; decoding or assertion
failures with an available decoder fail the test.

## Scratch tools linking Patchy's release libs

Scratch tools linking Patchy's release libs (e.g. flattening a PSD through the reader outside the test suite) compile with:

```powershell
cmd /s /c '<repo>\scripts\vs-env.bat -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c++20 /EHsc /O2 /MD /I <repo>\src <tool>.cpp /link /LIBPATH:<repo>\build\release patchy_psd.lib patchy_render.lib patchy_core.lib patchy_color.lib patchy_filters.lib gdi32.lib user32.lib advapi32.lib dwrite.lib'
```

`/MD` is required to match the libs; `advapi32`/`dwrite` are needed by `psd_document_io`'s font-resolution code.
