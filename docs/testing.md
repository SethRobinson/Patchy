# Testing and visual QA

Native visual QA and app-driving commands are in [visual-qa.md](visual-qa.md); README screenshot capture is in [readme-showcase.md](readme-showcase.md); the Linux foreign-drag rig is in [linux-testing.md](linux-testing.md).

Read before adding tests, changing test infrastructure, diagnosing suite-only failures, or driving Patchy for visual QA.

## Suite organization

`tests/test_harness.hpp` is the shared runner and check vocabulary; its one Win32 body (`suppress_crash_dialogs`) is in `tests/test_harness.cpp`, listed in every test executable, so `<windows.h>` stays out of the test TUs. The core binary groups tests in `tests/core/*_tests.cpp`. Each TU exports `<group>_tests()`; `tests/core/main.cpp` concatenates registrations in fixed order. Append to the appropriate vector; never use static self-registration (cross-TU order varies). Move shared Qt-free helpers into `tests/core/core_test_support.{hpp,cpp}` or `psd_test_support.{hpp,cpp}`, namespace `patchy::test`; do not duplicate them (`solid_rgb`, `solid_rgba` and `solid_rgb_document(w, h, layer, r, g, b)` already exist: never redefine them in a TU).

A test that needs a shown window with the historical startup document starts with `auto&& [window, canvas] = ShownWindowFixture{};` (`ui_test_support.hpp`): one line for `MainWindow window; show_window(window); auto* canvas = require_canvas(window);`, with the same lifetime as the local it replaces. Name the canvas even when unused (no compiler warns while one bound name is used). Keep the explicit local only for `show_window_empty` or a document opened before showing. `synthetic_dng.hpp` belongs only in TUs that call `synthetic_bayer_dng`.

The UI suite uses `tests/ui/*_tests.cpp`, declarations in `tests/ui/ui_test_groups.hpp`, and fixed ordering in `tests/ui/main.cpp`: contact-sheet/README tests consume earlier artifacts, and QSettings state crosses tests. Helpers: `tests/ui/ui_test_support.{hpp,cpp}`, namespace `patchy::test::ui`. `MainWindowTestAccess` (`tests/ui/ui_test_access.hpp`, bodies in `ui_test_access.cpp`; both compile once in the `patchy_ui_test_shared` OBJECT library with `ui_test_support.cpp` and the translation tests) is befriended by qualified name in `main_window.hpp`.

Both suites' `unicode_path_tests` and `ui_script_io_round_trips_unicode_path` share names in `tests/unicode_path_names.hpp`. In names and Qt-free TUs, spell non-ASCII as `\u`/`\U` escapes in `u8""`: only Qt-linked targets inherit `-utf-8`; raw bytes in core tests use the ANSI code page and raise C4819.

Recursive cleanup uses `tests/test_scratch_remove.hpp` (UI wrapper `remove_test_scratch_dir`):
targets strictly below `test-artifacts`, `qttest`, `.qttest` or system-temp `patchy*`, parent
links resolved before the containment check, final links removed without their targets. Node
uses host path APIs ([wasm.md](wasm.md)) and fails closed on resolution errors.

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

Never run two test processes (or a test process and the app) at once: they share the QSettings store, and a concurrent run rewrites preference keys mid-test. One suite at a time.

The QSettings store also persists across runs, and a killed run skips every customize-then-restore test's restore step. Any settings group one test customizes while another asserts its defaults without seeding them (the `hotkeys` group is the known case) must be removed by the bootstrap block in `tests/ui/main.cpp`; groups whose assertion sites all clear or seed their own keys first (`palettes`, `colorPanel`, `saveOptions`, `recentFiles`) need no bootstrap entry. `cleanup_after_visual_test` clears `newDocument` (its Recent row carries Artboards into the next New) and the active-tool and brush keys per test, Brush Mode included (a test that left Behind or Clear made every later stroke paint nothing).

Tests save PNG artifacts through `save_widget_artifact` / `capture_widget_artifact` into `test-artifacts/`. The app QSS is on the MainWindow, so a nullptr-parent dialog would grab Qt's default light widgets: every capture helper first prepends the active theme's sheet to such a window (`ensure_capture_themed`; pixel checks made before a capture need `make_themed_dialog_parent()`), then validates the tree with `capture_theme_mismatch` (unpolished widget, window without the app sheet unless a QTipLabel, a background or text color that is Qt's default or the other scheme's and no active palette role) and records the offender's class and objectName through `patchy::test::report_failure` instead of saving (never a throw: Qt's event dispatch); the sheet goes back after the grab. Exempt with their children (document content, user colors): objectName `inlineTextEditor`, `*ColorButton`, `*ColorSwatch`. New capture code uses these helpers, never a bare `grab()`; `ui_offscreen_dialog_sweep_is_themed_in_both_schemes` covers both schemes. Renaming an artifact means updating the contact-sheet list in `tests/ui/readme_screenshot_tests_classic.cpp`.

## Offscreen fonts and input

**Offscreen runs draw with Fusion; the Windows app draws with the Windows style.** Qt's offscreen platform names only Fusion, so the suite and `--headless` captures use it on every OS (Linux desktops do too). Fusion leaves the step buttons of a spin box framed by the app sheet (the Color panel's number boxes) blank; `AppStyle` (src/ui/app_style.cpp) widens the arrow rect it gets, and the suite installs `AppStyle` as main.cpp does (issue 404: captures showed blank buttons the app draws arrows on). When a capture differs from the app, suspect the style first; `QT_STYLE_OVERRIDE=Windows` on a `--headless` run shows the Windows look. Guard: `ui_app_style_draws_spin_box_step_arrows_on_fusion`.

**The offscreen platform on Windows rasterizes text through FreeType; a real window uses DirectWrite.** They disagree: DirectWrite applies a QFont stretch to the advances only and draws glyphs at design width (issue 20: a 103% "M" came out 3% narrow on screen). Text that must match on screen is drawn engine-independently (`draw_line_glyphs_pixel_aligned`, docs/text-render-calibration.md) and its DirectWrite acceptance is a fresh GUI instance of the release build (`PATCHY_NO_SINGLE_INSTANCE=1`, `--run-script` without `--headless`) exporting the layer for a pixel diff; `-platform windows:fontengine=freetype` reproduces the suite's engine.

The offscreen platform does not enumerate installed Windows fonts. Register required faces through `tests/test_fonts.hpp` or `QFontDatabase::addApplicationFont`; offscreen FreeType registers the typographic family (`ariblk.ttf` adds "Arial" style Black, never an "Arial Black" family). Never remove an application font mid-suite: invalidating an in-use font cache crashes. The Windows registry font rescue (`try_register_missing_system_font_family`) is off under offscreen unless `PATCHY_HEADLESS` is set (only a `--headless` app run sets it, and that run loads every installed font at startup: [font-resolution.md](font-resolution.md)). A test naming a family must also work on the Linux host (no Arial, Segoe UI, Calibri or Verdana): include `Liberation Sans` / `DejaVu Sans` in its candidates, since a text edit moves a layer with a missing family onto its substitute (`substituted_text_family`).

Because fonts are never removed, every registration is permanent suite state: new families change which face Qt's missing-family fallback picks, moving text metrics in every later test (`text_transform_commit_tests` pins committed rasters against that fallback and fails after a mass registration). Register only the faces a test needs; a large inventory runs in a child process: `ui_bundled_web_fonts_register_and_create_engines` spawns `patchy_ui_visual_tests.exe --bundled-web-fonts-probe` (handled in `tests/ui/main.cpp` before the QSettings bootstrap: the child never touches the parent's store).

FreeType may expose an OpenType typographic family rather than its familiar GDI family, such as Arial with style Black for `ariblk.ttf`. Use `available_text_family_style_match`; do not gate tests on `QFontDatabase::families().contains(...)`.

A writing system that no registered face covers (Thai or Japanese text in the suite or under `--headless`) makes Qt resolve the request to its glyph-box engine, whose family list is empty; `QRawFont::familyName()` indexes it unchecked and crashes. Check `QFontDatabase::families(system)` before asking a per-script `QRawFont` for its family (reference `text_family_draws_any_of`, main_window.cpp; pinned by `ui_script_text_layer_with_uncovered_script_does_not_crash`).

Offscreen does not clear `QApplication::keyboardModifiers()` after synthetic key events, and the stuck bit persists in the shared QApplication. Assert behavior through code that reads the current event's folded modifiers. `ui_brush_alt_shows_eyedropper_cursor` is the order-independent reference. QTest input (spontaneous) leaves its modifiers there on every platform, and on macOS so does a plain sent Ctrl key; `cleanup_after_visual_test` resets the state between tests, so only a test's own earlier input can leak into its later checks.

On macOS a menu popped up with `QMenu::popup` leaves the window's shortcuts unmatched for the rest of the test: send shortcut keys before any menu capture.

## Driving dialogs

- A test that answers a modal dialog arms `when_dialog_visible(name, drive)` (tests/ui/ui_test_support.hpp) before the action that opens it. Never arm a fixed-delay `QTimer::singleShot(N, ...)` that looks the dialog up: when the opener defers through the event loop (the smart filter Edit button posts a 0 ms timer, then builds the dialog) or pumps events before showing it, the delay fires first on a slow host and finds nothing. Three smart filter tests failed that way on the ARM64 host in the 1.23 release run; `ui_test_harness_when_dialog_visible_waits_for_a_deferred_opener` reproduces the race. The smart filter tests use the helper; the older fixed-delay drivers elsewhere are converted when they are next touched or when one flakes.
- The helper records a throw inside `drive` as a failure and rejects the dialog, so a failing check neither crosses Qt's event dispatch nor leaves the test parked in the dialog's loop.
- A bound on background work counts every worker that can be running: the Export for Web engine has two lanes, so a replaced request can leave two stale encodes started, not one (`ui_web_export_engine_drops_stale_requests`, macOS in 1.23). When a design adds a worker or lane, update the bounds that count them.
- A full-suite failure that passes on a rerun is a bug in the test or the code: diagnose it and fix the cause, never rerun it away (Seth, October 30, 2026). When it cannot be reproduced, make the failing check print the state that decides it (`print_shape_drag_state`) so the next occurrence explains itself, and record it in the release notes.

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
  open windows (a modal dialog nobody answers is the usual cause), and it exits 124. The
  perf harness arms the same watchdog per scenario (900 s) and on Windows also prints the
  main thread's stack, so a stalled scenario names its blocked frame; it also isolates its
  settings under `test-artifacts/perf-settings` and turns the open-time notices (missing fonts,
  missing links, the Affinity image-layers question, CMYK proof, update check) off for every
  scenario, as the UI runner does. A new
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

## Scratch tools linking Patchy's release libs

Scratch tools linking Patchy's release libs (e.g. flattening a PSD through the reader outside the test suite) compile with:

```powershell
cmd /s /c '<repo>\scripts\vs-env.bat -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c++20 /EHsc /O2 /MD /I <repo>\src <tool>.cpp /link /LIBPATH:<repo>\build\release patchy_psd.lib patchy_render.lib patchy_core.lib patchy_color.lib patchy_filters.lib gdi32.lib user32.lib advapi32.lib dwrite.lib'
```

`/MD` is required to match the libs; `advapi32`/`dwrite` are needed by `psd_document_io`'s font-resolution code.
