# Testing and visual QA

Read before adding tests, changing test infrastructure, diagnosing suite-only failures, or driving Patchy for visual QA.

## Suite organization

The core binary groups tests in `tests/core/*_tests.cpp`. Each TU exports `<group>_tests()`; `tests/core/main.cpp` concatenates registrations in fixed order. Append to the appropriate vector; never use static self-registration (cross-TU order varies). Move shared Qt-free helpers into `tests/core/core_test_support.{hpp,cpp}` or `psd_test_support.{hpp,cpp}`, namespace `patchy::test`; do not duplicate them.

The UI suite uses `tests/ui/*_tests.cpp`, declarations in `tests/ui/ui_test_groups.hpp`, and fixed ordering in `tests/ui/main.cpp`: contact-sheet/README tests consume earlier artifacts, and QSettings state crosses tests. Helpers: `tests/ui/ui_test_support.{hpp,cpp}`, namespace `patchy::test::ui`. `MainWindowTestAccess` (`tests/ui/ui_test_access.hpp`) is befriended by qualified name in `main_window.hpp`.

Both suites' `unicode_path_tests` and `ui_script_io_round_trips_unicode_path` share names in `tests/unicode_path_names.hpp`. In names and Qt-free TUs, spell non-ASCII as `\u`/`\U` escapes in `u8""`: only Qt-linked targets inherit `-utf-8`; raw bytes in core tests use the ANSI code page and raise C4819.

Recursive cleanup uses `tests/test_scratch_remove.hpp` (UI wrapper:
`remove_test_scratch_dir`). Targets must be strictly below `test-artifacts`,
`qttest`, `.qttest`, or system-temp `patchy*`; roots are protected. Resolve parent
links before checking containment; remove final links without their targets.
Node uses host path APIs ([wasm.md](wasm.md)), failing closed on resolution errors
and dangling parents. Core filter `test_scratch_remove` covers missing/Unicode
paths and link safety; unavailable symlink creation reports a capability skip.

Split groups exceeding ~3,000 lines into `<group>_tests_<theme>.cpp`, exporting
`<group>_tests_partN()`. Keep `<group>_tests.cpp` as an aggregator preserving the
original registration order; append new tests to the appropriate part's vector.
Move helpers shared across parts into `<group>_test_support.{hpp,cpp}`; single-part
helpers stay anonymous. Core parts must stay under ~3,000 lines: the target has no
`/bigobj`.

Local-fixture tests skip on a remote machine until the untracked `local-test-fixtures` is copied there: from the repo root `tar -cf - local-test-fixtures | ssh <host> 'tar -xf - -C ~/patchy/src'` (Git Bash; macOS tar drops four harmless `__MACOSX/._*` entries). Filter Foundry plug-ins: `scripts/fetch-test-plugins` per machine. A synced corpus enables the face-gated text tests; the gates and tolerances in [platform.md](platform.md) keep them passing (a missing face raises an unanswerable Missing Font prompt).

The harnesses turn deep editing off by default (`set_deep_editing_default(false)`), so tests written for 8-bit opening keep their meaning; deep tests set `set_deep_editing_override` ([high-bit-depth.md](high-bit-depth.md)).

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

Run `patchy_ui_visual_tests.exe` with `QT_QPA_PLATFORM=offscreen`. The harness forces offscreen itself; `PATCHY_UI_TEST_PLATFORM=<qpa plugin>` (`cocoa`, `windows`, `xcb`) runs it on a real platform instead, the only way to reach native menubar and window-activation code (the issue 29 macOS crash; `NSZombieEnabled=YES` aborts on a freed Cocoa object). Screens and fonts differ there: run a filter, never the whole suite.

Both suites (wasm core too) share `run_test_suite` (`tests/test_harness.hpp`): `<binary> [--list] [--exact] [filter]`. The filter is a comma-separated list of name substrings (any match runs), which reproduces ordered cross-test interactions. `--exact` matches whole names; `--list` prints the selection without running it. A filter item that selects no test fails the run (exit 2): a typo never reads as a pass. The UI suite also reads `PATCHY_UI_TEST_FILTER`; the command line wins.

A run ends with `[SUMMARY] executed N, passed P, failed F, skipped S in T s`. Exit codes: 0 nothing failed (skips allowed), 1 a test failed, 2 a bad command line or empty selection, 124 a timeout. `[FAIL]` goes to stderr, everything else to stdout: capture both and judge a run by exit code, `[FAIL]` lines and counts. Report skips with results: an all-skip run checked nothing.

A test whose optional input is absent (untracked fixture, missing face, a build or platform without the feature) calls `patchy::test::skip(reason)` or `skip_unless_exists(path)` before asserting; the runner prints `[SKIP] <test>: <reason>`. Never return early or print `[SKIP]` yourself: the runner then reports `[PASS]` for nothing. A missing optional input never fails a test; one that checked part of its work prints a `  note:` line and passes. `TestSkipped` is not a `std::exception`: never skip inside a `try` with `catch (...)`. UI face gates: `skip_without_font_face`, `skip_without_psd_text_face`, `skip_without_arial_for_psd_text_preview` (`missing_*_reason` for partial checks).

Never run two test processes (or a test process and the app) at once: they share the QSettings store, and a concurrent run rewrites preference keys mid-test (`ui_language_saved_preference_overrides_system_language` saw its saved language clobbered). One suite at a time.

The QSettings store also persists across runs, and a killed run skips every customize-then-restore test's restore step. Any settings group one test customizes while another asserts its defaults without seeding them (the `hotkeys` group is the known case) must be removed by the bootstrap block in `tests/ui/main.cpp`; groups whose assertion sites all clear or seed their own keys first (`palettes`, `colorPanel`, `saveOptions`, `recentFiles`) need no bootstrap entry. `cleanup_after_visual_test` clears `newDocument` (its Recent row carries Artboards into the next New) and the active-tool and brush keys per test.

Tests save PNG artifacts through `save_widget_artifact` / `capture_widget_artifact` into `test-artifacts/`. The app QSS is on the MainWindow, so a nullptr-parent dialog would grab Qt's default light widgets: every capture helper first prepends the active theme's sheet to such a window (`ensure_capture_themed`; pixel checks made before a capture need `make_themed_dialog_parent()`), then validates the tree with `capture_theme_mismatch` (unpolished widget, window without the app sheet unless a QTipLabel, a background or text color that is Qt's default or the other scheme's and no active palette role) and records the offender's class and objectName through `patchy::test::report_failure` instead of saving (never a throw: Qt's event dispatch); the sheet goes back after the grab. Exempt with their children (document content, user colors): objectName `inlineTextEditor`, `*ColorButton`, `*ColorSwatch`. New capture code uses these helpers, never a bare `grab()`; `ui_offscreen_dialog_sweep_is_themed_in_both_schemes` covers both schemes. Renaming an artifact means updating the contact-sheet list in `tests/ui/readme_screenshot_tests_classic.cpp`.

## Offscreen fonts and input

**The offscreen platform on Windows rasterizes text through FreeType; a real window uses DirectWrite.** They disagree: DirectWrite applies a QFont stretch to the advances only and draws glyphs at design width (issue 20: a 103% "M" came out 3% narrow on screen). Text that must match on screen is drawn engine-independently (`draw_line_glyphs_pixel_aligned`, docs/text-render-calibration.md) and its DirectWrite acceptance is a fresh GUI instance of the release build (`PATCHY_NO_SINGLE_INSTANCE=1`, `--run-script` without `--headless`) exporting the layer for a pixel diff; `-platform windows:fontengine=freetype` reproduces the suite's engine.

The offscreen platform does not enumerate installed Windows fonts. Register required faces through `tests/test_fonts.hpp` or `QFontDatabase::addApplicationFont`; offscreen FreeType registers the typographic family (`ariblk.ttf` adds "Arial" style Black, never an "Arial Black" family). Never remove an application font mid-suite: invalidating an in-use font cache crashes. The Windows registry font rescue (`try_register_missing_system_font_family`) is off under offscreen unless `PATCHY_HEADLESS` is set (only a `--headless` app run sets it). A test naming a family must also work on the Linux host (no Arial, Segoe UI, Calibri or Verdana): include `Liberation Sans` / `DejaVu Sans` in its candidates, since a text edit moves a layer with a missing family onto its substitute (`substituted_text_family`).

Because fonts are never removed, every registration is permanent suite state: new families change which face Qt's missing-family fallback picks, moving text metrics in every later test (`text_transform_commit_tests` pins committed rasters against that fallback and fails after a mass registration). Register only the faces a test needs; a large inventory runs in a child process: `ui_bundled_web_fonts_register_and_create_engines` spawns `patchy_ui_visual_tests.exe --bundled-web-fonts-probe` (handled in `tests/ui/main.cpp` before the QSettings bootstrap: the child never touches the parent's store).

FreeType may expose an OpenType typographic family rather than its familiar GDI family, such as Arial with style Black for `ariblk.ttf`. Use `available_text_family_style_match`; do not gate tests on `QFontDatabase::families().contains(...)`.

A writing system that no registered face covers (Thai or Japanese text in the suite or under `--headless`) makes Qt resolve the request to its glyph-box engine, whose family list is empty; `QRawFont::familyName()` indexes it unchecked and crashes. Check `QFontDatabase::families(system)` before asking a per-script `QRawFont` for its family (reference `text_family_draws_any_of`, main_window.cpp; pinned by `ui_script_text_layer_with_uncovered_script_does_not_crash`).

Offscreen does not clear `QApplication::keyboardModifiers()` after synthetic key events, and the stuck bit persists in the shared QApplication. Assert behavior through code that reads the current event's folded modifiers. `ui_brush_alt_shows_eyedropper_cursor` is the order-independent reference.

## Failure and lifetime traps

- Timer-driven checks wait for observable state with a bounded deadline: a busy machine
  delivers fewer timer ticks than a fixed sleep assumes (the Airbrush opacity-cap test).
- A wall-clock limit in a test is a hang guard, never a performance bound: give it
  minutes (`kRawDialogDeadlineMs` in `camera_raw_heif_tests.cpp` is 120 s). Never sleep
  to synchronize; wait for the state (`process_events_until`, or a script's marker file
  as `protocol_edges` in `tests/mcp_client_tests.py` does).
- Every test runs under a per-test timeout (`TestWatchdog`, tests/test_harness.hpp):
  300 s core, 600 s UI, four times that in Debug; `PATCHY_TEST_TIMEOUT_SECONDS` overrides,
  0 disables. On expiry the run prints `[FAIL] <test>: timed out`, the UI suite lists its
  open windows (a modal dialog nobody answers is the usual cause), and it exits 124. A new
  prompt must stay off under tests and unattended runs. `PATCHY_TEST_TIMEOUT_SELFTEST=1`
  adds a self-test that hangs on purpose.
- A debounced worker's start is not a fixed offset from the trigger. To prove work runs
  off the event loop, park the worker, wait for it to start, and check that a UI timer
  fires while it is still parked
  (`ui_filter_gallery_heavy_thumbnail_queue_yields_to_event_loop`).

- A test that sets an environment variable declares an `EnvironmentVariableRestorer` (tests/ui/ui_test_support.hpp) before the `qputenv`, never a trailing `qunsetenv`: a failed `CHECK` skips the cleanup and the value leaks into every later test (a leaked 300 ms `PATCHY_SCRIPT_TIMEOUT_MS` once killed 16 later scripted runs).
- The test `CHECK()` macro throws. A failure while a MainWindow still owns an open inline text editor can abort during unwind without printing a `[FAIL]` line. Commit or close the editor before assertions that may throw.
- Never let a driver lambda (a `QTimer::singleShot` body or any slot) throw across Qt event dispatch; Qt does not support it, and on macOS the suite aborts in the CFRunLoop frames. Wrap the driver body in try/catch and pass `std::current_exception()` to `patchy::ui::unwind_non_modal_dialog_loop` while the code under test is parked in `run_non_modal_dialog` (reference: `ui_filter_gallery_unwinding_call_disarms_in_flight_renders`).
- Clicking a layer-row content or mask thumbnail may rebuild and delete the row widget between press and release. Use `click_layer_row_thumbnail(...)`, which refetches the widget for both events; never retain the old pointer.
- If the UI suite dies with an access violation, read the symbolized stack appended by the dbghelp vectored handler in `tests/ui/main.cpp`. It also writes `test-artifacts/crash-<pid>.dmp`; when the walk prints no frames (a fault inside the heap manager), symbolize it offline against `build\release`.
- A crash that occurs only in the full ordered suite is usually an order-dependent heap error. Use the `linux-asan` procedure in [platform.md](platform.md); never reorder or skip tests to conceal it.
- Tests that enable `imports/showPsdWarningsAndInfo` need a repeating QTimer notice dismisser. A one-shot can fire during open progress and leave the suite hung; see [file-formats.md](file-formats.md) under Import notices.
- Platform-specific skips and their reasons are maintained in [platform.md](platform.md).

## README screenshots

`scripts\make-readme-screenshots.ps1` regenerates `docs/images/screenshots/`. The current
showcase, per-image inputs, and regeneration commands are listed in
[readme-showcase.md](readme-showcase.md). KPT is an explicit driver scene
(`-Scene plugin_kpt5`, or `-IncludeLegacyPlugins` for a full run) using an isolated
portable host and fixture copy; it combines direct Patchy and plug-in window captures.
Two pipelines:

- **Script-driven scenes** (`scripts/dev/readme-shots/*.js`, listed in the driver's
  `$jsScenes` table): a fresh unattended `patchy.exe --run-script` run stages the UI with the
  `patchy.ui` staging APIs on the REAL windows platform, so every installed font renders
  (offscreen enumerates none). The driver pins DPI (`QT_ENABLE_HIGHDPI_SCALING=0`,
  `QT_FONT_DPI=96`), sets `PATCHY_NO_SINGLE_INSTANCE=1`, and isolates settings with
  `PATCHY_SETTINGS_DIR`, so a run never touches the user's Patchy state. The window shows
  on screen for a few seconds per scene.
- **Offscreen test scenes** (everything not yet migrated): the `shot_readme_*` scenes in
  `patchy_ui_visual_tests` run offscreen and their artifacts are copied out. New scenes should
  be authored as scripts in the first pipeline; the remaining test scenes migrate over time and
  stay in the suite as regression smoke tests either way (the driver skips copying a test
  artifact whose scene has a script-driven owner). Scenes that picture a modal dialog the
  scripting surface cannot open stay here permanently: `shot_readme_image_trace` captures the
  Trace Image to Shapes dialog, which `layer.traceToShapes` bypasses and `app.runCommand`
  would block a script on; `shot_readme_long_shadow` opens the Layer Style dialog
  beside generated editable text with a live continuous shadow.

`scripts/dev/readme-shots/plugin-dialog.js` (driver scene `plugin_dialog`) produces
`docs/images/screenshots/plugin_dialog.png`, the dialog of a third-party 8bf plug-in
(Mehdi's Absolute Color from `local-test-fixtures/photoshop-plugins/mehdi`) captured through
`layer.applyPlugin(id, {captureDialog})` on Windows. The plug-in is not committed, so the
driver skips the scene with a `[SKIP]` line where that folder is missing; its window-targeted
capture and automatic dialog answers are authorized Patchy automation, not desktop control.

Both pipelines round the corners of the window they captured, because DWM rounds Patchy's
frameless windows in the compositor and a `QWidget::grab()` is therefore square. The offscreen
side does it in `save_readme_shot` and `draw_readme_overlay` (which also rounds the shadow it
fakes under a composited dialog); the driver does it to the script-driven PNGs in
`Set-RoundedWindowCorners`. Both use 8 px, `DWMWCP_ROUND`'s radius at 96 DPI, and leave the
corner pixels transparent so a shot reads as a window on a light or dark page. Change the
radius in both places or the two pipelines drift.

## Native visual QA and app-driving commands

The permission rule is in AGENTS.md: automation confined to Patchy and its hosted
plug-in windows (KPT included) is authorized; desktop screenshots (even cropped),
global input (`SendInput`, cursor moves), Computer Use, and other applications need
explicit authorization in the current request, and an MCP wrapper does not change
that. Drive Patchy through its scripting, CLI, MCP, or messages targeted to its own
windows; `patchy.ui.captureWindow`, `QWidget::grab`, and the plug-in helper's
`PrintWindow` capture only the intended windows.

Keep unattended runs in task-owned instances with isolated settings, offscreen where
possible. Permission for Patchy input never covers discarding unsaved work, closing
the user's running app/connector, or stopping unrelated processes.

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

Run from the repository root; artifacts stay under `test-artifacts/mcp`. The test
uses only owned offscreen processes and also accepts a staged package's connector
(resource discovery without source-relative paths). The shipped connector needs no Python.
Mac host: `.deps/mcp-client-py312/bin/python` (system Python 3.9 cannot install the MCP
dependency); linux host: `.deps/mcp-client/bin/python`; run
`nice -n 10 <python> tests/mcp_client_tests.py <connector>` from the remote repository root.
The suite covers owned and attached workspaces, competing clients, cancellation,
disconnect cleanup, app restart, stale tokens, and shared history; Unix crash
simulations remove their own sockets.
Pass `--attachment-recovery-only` after the connector path to reproduce offline
discovery, late app startup, restart with fresh state tokens, and interruption
without replay. This test owns its offscreen apps and requires no prior artifacts.
The client suite also checks `--visible` option handling and mode/preview metadata on an
explicit offscreen backend. A real visible smoke test may use a task-owned Patchy instance
under the rules above.
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

`patchy.exe --screenshot <out.png>` captures the running instance without raising or focusing it; `--screenshot-widget <qtObjectName>` and/or `--screenshot-rect x,y,w,h` narrow it; positional files open a document. The invoker exits at once: poll for the output. With no instance running, Patchy opens, waits about 1.5 s, captures and exits 0 (3 on failure). Add `--language <code>` (with `--headless`, so no running instance is reused) to capture a UI language without changing the saved preference ([localization.md](localization.md)). Never run `patchy.exe --help` or `--version` during verification: the Windows GUI build has no console, so Qt shows them in a message box over whatever Seth is doing.

`patchy.exe --stress-test[=quick|small|standard|huge] [--stress-report-dir <dir>] [--stress-depth 16|32]` builds the deterministic performance scene (16 or 32 bits optional) and exits. Reports default to `%APPDATA%\Patchy\stress-reports\` (`stress-latest.json`). Quick (1024 px) is for iteration, standard (4096 px) for full-scale measurements; real timings need a real screen ([performance.md](performance.md)).

`patchy.exe --headless ...` runs any of those modes on Qt's offscreen platform. It never forwards to a running instance, so the exit code and `--script-output` file are the run's own; prompts are suppressed and sound muted. Leave it off when a capture must show the real platform and installed fonts (offscreen sees only bundled fonts; a Windows headless run loads installed fonts from the registry on the first text request).

Useful diagnostic variables:

- `PATCHY_NO_SINGLE_INSTANCE=1` allows multiple instances.
- `PATCHY_FAKE_SCANNER_FILE=<path>` bypasses native scanner acquisition in tests.
- `PATCHY_REV_TRACE=1` logs revision bumps.
- `PATCHY_ZOOM_TRACE=1` logs paint and zoom phases over 2 ms.
- `PATCHY_STYLE_MASK_CACHE_OFF=1` disables the style-mask cache.
- `PATCHY_RENDER_SINGLE_THREADED=1` forces byte-stable sequential rendering.
- `PATCHY_RENDER_THREADS=<n>` caps every parallel fan-out at n workers in place of the hardware thread count (perf harness emulation of a low-core machine; the transform proxy gate scales with it).
- `PATCHY_PROCESSING_OVERLAY_MIN_PIXELS` overrides the processing-overlay threshold.
- `PATCHY_RENDER_BACKEND=auto|webgpu|cpu|opengl|vulkan|metal|d3d11|d3d12` selects the graphics preference. `auto` attempts the optional Dawn/WebGPU document compositor when it was found at configure time and then uses Qt Quick/RHI for presentation; `webgpu` makes that preference explicit. `PATCHY_GPU_CANVAS=auto|cpu` remains an alias. A hardware scene graph or Dawn adapter can still fall back to the complete CPU compositor when the document is outside the GPU capability matrix, including malformed Blend If payloads.
- The CTest registration for `patchy_ui_visual_tests` sets `QT_QPA_PLATFORM=offscreen;PATCHY_RENDER_BACKEND=cpu`. This keeps byte-sensitive UI/export tests CPU-authoritative even on a machine with Dawn installed. Exercise WebGPU separately with an explicit `PATCHY_RENDER_BACKEND=webgpu` or `auto` run.
- `PATCHY_ENABLE_WEBGPU=ON` enables CMake discovery of an installed Dawn package; a missing package is expected and leaves the same binary on the Qt RHI/CPU path. `scripts/build-gpu.sh linux-release` performs the configure, build, core tests, and the deterministic backend-selection test in one step; set `PATCHY_QT_PREFIX` and `PATCHY_DAWN_PREFIX` when the dependencies are outside the preset. `scripts/build-dawn.sh --print-config` checks the pinned lock without network access, while `scripts/build-gpu.sh linux-release --dawn-only` builds and verifies the optional Dawn prefix. For a real Dawn build, use `--with-dawn`, inspect the configure output for the Dawn target, then run `PATCHY_NO_SINGLE_INSTANCE=1 PATCHY_RENDER_BACKEND=auto QSG_INFO=1 ./build/linux-release/patchy` with a real native display. Confirm a line containing `Patchy WebGPU document compositor active on ... via ...` before treating WebGPU composition as exercised. See [GPU build and dependency guide](gpu-build.md) and [GPU troubleshooting](gpu-troubleshooting.md) for the full setup and fallback procedure.
- `PATCHY_NO_SOUND=1` suppresses script audio; offscreen suites rely on it.
- `PATCHY_UPDATE_MANIFEST_URL=<url>` replaces the update check's manifest URL (a loopback server in the update-check tests). An unresolvable host such as `https://x.local/m.json` holds a resolver call open for seconds and reproduced the issue 48 macOS quit freeze (quit in that window and time the exit). `PATCHY_NO_UPDATE_CHECK=1` removes the check, like a `PATCHY_STORE_BUILD` build.
- `PATCHY_SETTINGS_DIR=<dir>` redirects the app's ini settings store (automation isolation).
- `PATCHY_RECOVERY_DIR=<dir>` redirects the automatic document recovery store (the UI suite uses `test-artifacts/recovery`); `PATCHY_RECOVERY_INTERVAL_MS=<ms>` overrides the timer interval ([document-recovery.md](document-recovery.md)). `PATCHY_USER_FONTS_DIR=<dir>` moves the font store ([fonts.md](fonts.md)); `PATCHY_USER_SCRIPTS_DIR=<dir>` the user scripts folder (the UI suite uses `test-artifacts/user-scripts/<pid>`).
- `PATCHY_HEADLESS=1` marks a `--headless` app run (main.cpp sets it). Never set it for the suites: it re-enables the registry font rescue and makes layouts machine-dependent.
- `QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES=1` makes `patchy.exe --help` and command-line parse errors print to the redirected stdout/stderr instead of a modal Win32 message box (patchy.exe is a GUI-subsystem binary), which is what an automated `--help` check needs.
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

## Foreign drags and the KDE rig (GitHub issue 62)

On Wayland the old dock column dragged its panels with a real `QDrag`, so every enabled `acceptDrops` widget under the cursor saw drag events that carried no layers. The panels no longer float or drag, but any foreign drag (a file manager's, another app's) still reaches those widgets the same way.

**Never repolish a widget from inside its own event filter.** `installEventFilter` puts a filter ahead of those already installed, and KDE's Breeze style installs filters in `polish()`. A filter that calls `style()->unpolish/polish` on the watched widget is moved behind Qt's loop cursor and runs again for the same event; done unconditionally it never ends (issue 62: the layer action buttons' drag filter froze the Flatpak on KDE). Change the property only when it differs and repolish one event-loop hop later (`handle_layer_action_button_drag_event`; `ui_layer_action_button_foreign_drag_never_repolishes_in_filter` stands a filter-installing proxy style in for Breeze). It needs Breeze (the Flatpak's KDE runtime, `XDG_CURRENT_DESKTOP=KDE`) and enabled buttons (a document open). KWin's "not responding" kill sends SIGABRT: that coredump is a hang.

KDE rig on the Linux build host, with no root and no desktop control:

- KWin: a private apt configuration (own `Dir::State`, `Dir::Cache`, empty status file, the host's Ubuntu keyring) pointed at a newer Ubuntu series downloads `kwin-wayland`, `qt6-wayland` and `plasma-wayland-protocols` plus dependencies; `dpkg-deb -x` each into a prefix. Run it through the prefix's own loader: `<prefix>/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 --library-path <prefix libs> <prefix>/usr/bin/kwin_wayland --virtual --width 1600 --height 1000 --socket <name> --no-lockscreen --no-global-shortcuts --no-kactivities` under `dbus-run-session`, with `QT_PLUGIN_PATH`, `XDG_DATA_DIRS`, `XKB_CONFIG_ROOT` pointing into the prefix and `KWIN_FORCE_OWN_QPA=1` (the loader hides the executable name), `KWIN_COMPOSE=Q`, `KWIN_WAYLAND_NO_PERMISSION_CHECKS=1`, private `XDG_CONFIG_HOME`.
- Input: a client of `org_kde_kwin_fake_input` (authenticate, `pointer_motion_absolute`, `button` 272); a few dozen lines of Python over the Wayland socket are enough. Releasing happens when the client disconnects, so keep one process per gesture.
- App: `WAYLAND_DISPLAY=<name> flatpak run --user --env=XDG_CURRENT_DESKTOP=KDE --env=PATCHY_SETTINGS_DIR=/tmp/x --env=PATCHY_NO_SINGLE_INSTANCE=1 com.rtsoft.patchy <image>`. Add `--devel --command=gdb` for stacks (SIGINT the app's host PID to sample), or `--command=<host build>/patchy --env=LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu` to run a fresh host build on the runtime's Qt (Breeze loads). `WAYLAND_DEBUG=1` traces the protocol; `patchy --screenshot` captures a native instance.

GNOME variant: `gnome-shell --headless --wayland --no-x11 --unsafe-mode --virtual-monitor 1600x1000 --wayland-display <name>` under `dbus-run-session` (without `--no-x11` the shell deadlocks in PulseAudio's X11 lookup), driven via `org.gnome.Mutter.RemoteDesktop` and `org.gnome.Shell.Eval`/`Screenshot`. Script: `scripts/dev/wayland-gnome/gnome-rig.sh`. `start-shell` publishes `WAYLAND_DISPLAY` to the rig bus before the shell activates the portals (docs/wayland.md).

Stop every rig process by the PID recorded at launch. A name-based kill (`pgrep -n -x gnome-shell`) once took down the host's own desktop shell.
