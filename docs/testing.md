# Testing and visual QA

Read this before adding tests, changing test infrastructure, diagnosing suite-only failures, or driving Patchy for visual verification.

## Suite organization

The core suite is one binary split across `tests/core/*_tests.cpp`, one TU per thematic group. Each TU ends with a `<group>_tests()` registration function; `tests/core/main.cpp` concatenates them in a fixed order. Never use static self-registration because cross-TU initialization order would reorder the suite. Append tests to the correct group registration vector. Shared Qt-free helpers live in `tests/core/core_test_support.{hpp,cpp}` and `psd_test_support.{hpp,cpp}` under `namespace patchy::test`; move shared helpers there rather than copying them.

The UI suite follows the same design in `tests/ui/*_tests.cpp`. Registrations are declared in `tests/ui/ui_test_groups.hpp` and concatenated by `tests/ui/main.cpp` in a load-bearing order: contact-sheet and README tests consume artifacts written earlier, and QSettings state intentionally crosses tests. Shared helpers live in `tests/ui/ui_test_support.{hpp,cpp}` under `namespace patchy::test::ui`. `MainWindowTestAccess` in `tests/ui/ui_test_access.hpp` is befriended by its qualified name in `main_window.hpp`.

Unicode and special-character path tests (`unicode_path_tests` in both suites, plus `ui_script_io_round_trips_unicode_path`) share their file names through `tests/unicode_path_names.hpp`. Spell non-ASCII in those names and in any Qt-free TU as `\u`/`\U` escapes inside `u8""` literals: `patchy_core_tests` compiles without `-utf-8` (only Qt-linked targets inherit it), so raw UTF-8 bytes would be read through the ANSI code page and raise C4819.

Groups that outgrew ~3,000 lines are split into part files (`<group>_tests_<theme>.cpp`, each exporting `<group>_tests_partN()`); the original `<group>_tests.cpp` stays as a small aggregator whose exported function concatenates the parts in the original registration order, so the suite order is unchanged. Add a new test to the correct part file's registration vector, keeping the group's overall order intact. Helpers shared by two or more parts of one group live in that group's `<group>_test_support.{hpp,cpp}` (moved, never copied); helpers used by one part stay in that part's anonymous namespace. `patchy_core_tests` has no `/bigobj`, so core part files must stay under ~3,000 lines.

Local-fixture tests skip on a remote machine until `local-test-fixtures` is copied there, because that directory is deliberately untracked. Sync it from the repo root with `tar -cf - local-test-fixtures | ssh <host> 'tar -xf - -C ~/patchy/src'` (Git Bash; macOS tar drops four `__MACOSX/._*` AppleDouble entries, which are not test inputs). The snapshot checkout leaves untracked files alone, so one sync persists across later `remote-build.ps1` runs. Read the per-platform consequences in [platform.md](platform.md) before doing this: a synced corpus turns previously skipped text tests into failures and one hang. The repository-wide fixture sourcing rule lives in `AGENTS.md`.

## Running and filtering

Run `patchy_ui_visual_tests.exe` with `QT_QPA_PLATFORM=offscreen`. Both release test binaries accept a name substring as their first argument. The UI suite also reads `PATCHY_UI_TEST_FILTER`; there is no `--test` flag. The UI filter may also be a comma-separated list of substrings (a test runs if its name contains any of them), which is how to reproduce ordered cross-test interactions: select the state-leaking test and its victim in one run. The core suite takes a single substring only.

Never run two test processes (or a test process and the app) at the same time: they share the QSettings store, and a concurrent run rewrites preference keys mid-test, producing failures such as `ui_language_saved_preference_overrides_system_language` seeing its saved language clobbered. Run suites sequentially.

The QSettings store also persists across runs, and a killed run skips every customize-then-restore test's restore step. Any settings group that one test customizes while another test asserts its defaults without seeding them (the `hotkeys` group is the known case) must be removed by the bootstrap block in `tests/ui/main.cpp`; groups whose assertion sites all clear or seed their own keys first (`palettes`, `colorPanel`, `saveOptions`, `newDocument`, `recentFiles`) need no bootstrap entry.

Tests save PNG artifacts through `save_widget_artifact(...)` into `test-artifacts/` beside the binary. Inspect them directly when verifying rendering. Renaming an artifact also requires updating the contact-sheet list in `tests/ui/readme_screenshot_tests_classic.cpp` (the readme_screenshot_tests group is split into part files behind an order-preserving aggregator); stale files in long-lived build directories can otherwise hide the mismatch.

## Offscreen fonts and input

The offscreen platform does not enumerate installed Windows fonts. Register required faces through `tests/test_fonts.hpp` or `QFontDatabase::addApplicationFont`. Never remove an application font during the suite because invalidating an in-use font cache can crash it.

Because fonts are never removed, every registration is permanent suite state: newly present families change which face Qt's missing-family fallback picks, which moves text metrics in every later test (the PSD text re-edit tests in `text_transform_commit_tests` pin committed rasters against that fallback and fail if a mass registration runs first). Register only the faces a test actually needs. A test that must register a large inventory runs it in a child process instead: `ui_bundled_web_fonts_register_and_create_engines` spawns `patchy_ui_visual_tests.exe --bundled-web-fonts-probe` (handled in `tests/ui/main.cpp` before the QSettings bootstrap, so the child never touches the parent's settings store).

FreeType may expose an OpenType typographic family rather than its familiar GDI family, such as Arial with style Black for `ariblk.ttf`. Use `available_text_family_style_match`; do not gate tests on `QFontDatabase::families().contains(...)`.

Offscreen does not clear `QApplication::keyboardModifiers()` after synthetic key events, and the stuck bit persists in the shared QApplication. Assert behavior through code that reads the current event's folded modifiers. `ui_brush_alt_shows_eyedropper_cursor` is the order-independent reference.

## Failure and lifetime traps

- The test `CHECK()` macro throws. A failure while a MainWindow still owns an open inline text editor can abort during unwind without printing a `[FAIL]` line. Commit or close the editor before assertions that may throw.
- The test binaries can exit 0 even when tests fail. Never trust the exit code alone; grep the output for `[FAIL]` to judge a run.
- Never let a driver lambda (a `QTimer::singleShot` body or any slot) throw across Qt event dispatch; Qt does not support it, and on macOS the suite aborts in the CFRunLoop frames. Wrap the driver body in try/catch and pass `std::current_exception()` to `patchy::ui::unwind_non_modal_dialog_loop` when the code under test is parked in `run_non_modal_dialog`. `ui_filter_gallery_unwinding_call_disarms_in_flight_renders` is the reference.
- Clicking a layer-row content or mask thumbnail may rebuild and delete the row widget between press and release. Use `click_layer_row_thumbnail(...)`, which refetches the widget for both events; never retain the old pointer.
- If the UI suite dies with an access violation, read the symbolized stack appended by the dbghelp vectored handler in `tests/ui/main.cpp`.
- A crash that occurs only in the full ordered suite is usually an order-dependent heap error. Use the `linux-asan` procedure in [platform.md](platform.md); never reorder or skip tests to conceal it.
- Tests that enable `imports/showPsdWarningsAndInfo` need a repeating QTimer notice dismisser. A one-shot can fire during open progress and leave the suite hung; see [file-formats.md](file-formats.md) under Import notices.
- Platform-specific skips and their reasons are maintained in [platform.md](platform.md).

## README screenshots

`scripts\make-readme-screenshots.ps1` regenerates `docs/images/screenshots/`. Two pipelines:

- **Script-driven scenes** (`scripts/dev/readme-shots/*.js`, listed in the driver's
  `$jsScenes` table): a fresh unattended `patchy.exe --run-script` run stages the UI with the
  `patchy.ui` staging APIs (setWindowSize/setSidePanelWidth/captureWindow/setStatusMessage,
  plus the activeLayer panel reveal) on the REAL windows platform, so every installed font
  renders. Offscreen enumerates no installed fonts, which is why any scene whose document
  needs non-stock faces (the Affinity tips.af scene's Futura BT and FZ Script families) must
  live in this pipeline. The driver pins DPI (`QT_ENABLE_HIGHDPI_SCALING=0`, `QT_FONT_DPI=96`),
  sets `PATCHY_NO_SINGLE_INSTANCE=1`, and isolates settings with `PATCHY_SETTINGS_DIR` (an
  app-level env hook in src/app/main.cpp that redirects the ini-backed `app_settings()` store)
  so a run never touches the user's real Patchy state. The app window appears on screen for a
  few seconds per scene.
- **Offscreen test scenes** (everything not yet migrated): the `shot_readme_*` scenes in
  `patchy_ui_visual_tests` run offscreen and their artifacts are copied out. New scenes should
  be authored as scripts in the first pipeline; the remaining test scenes migrate over time and
  stay in the suite as regression smoke tests either way (the driver skips copying a test
  artifact whose scene has a script-driven owner).

Both pipelines round the corners of the window they captured, because DWM rounds Patchy's
frameless windows in the compositor and a `QWidget::grab()` is therefore square. The offscreen
side does it in `save_readme_shot` and `draw_readme_overlay` (which also rounds the shadow it
fakes under a composited dialog); the driver does it to the script-driven PNGs in
`Set-RoundedWindowCorners`. Both use 8 px, `DWMWCP_ROUND`'s radius at 96 DPI, and leave the
corner pixels transparent so a shot reads as a window on a light or dark page. Change the
radius in both places or the two pipelines drift.

## Native visual QA and app-driving commands

Never use Computer Use, desktop automation, or input injection for native QA without Seth's explicit authorization in the current request. Use Patchy's command-line control surfaces and inspect their outputs directly.

`patchy.exe --screenshot <out.png>` captures the running instance without raising or focusing it. Add `--screenshot-widget <qtObjectName>` and/or `--screenshot-rect x,y,w,h` to narrow the capture, and combine it with positional files to open a document. The invoking process exits immediately, so poll for the output. If no instance is running, Patchy opens, waits about 1.5 seconds, captures, and exits with code 0 on success or 3 on failure.

`patchy.exe --stress-test[=quick|small|standard|huge] [--stress-report-dir <dir>]` builds the deterministic performance scene and exits. Reports default to `%APPDATA%\Patchy\stress-reports\`; read `stress-latest.json`. Use quick at 1024 px for iteration and standard at 4096 px for full-scale measurements. Meaningful timings require a real screen. See [performance.md](performance.md).

Useful diagnostic variables:

- `PATCHY_NO_SINGLE_INSTANCE=1` allows multiple instances.
- `PATCHY_FAKE_SCANNER_FILE=<path>` bypasses native scanner acquisition in tests.
- `PATCHY_REV_TRACE=1` logs revision bumps.
- `PATCHY_ZOOM_TRACE=1` logs paint and zoom phases over 2 ms.
- `PATCHY_STYLE_MASK_CACHE_OFF=1` disables the style-mask cache.
- `PATCHY_RENDER_SINGLE_THREADED=1` forces byte-stable sequential rendering.
- `PATCHY_PROCESSING_OVERLAY_MIN_PIXELS` overrides the processing-overlay threshold.
- `PATCHY_NO_SOUND=1` suppresses script audio; offscreen suites rely on it.
- `PATCHY_SETTINGS_DIR=<dir>` redirects the app's ini settings store (automation isolation).
- `PATCHY_UI_TEST_FILTER` selects a UI test substring.
- `PATCHY_UI_PROFILE=1` prints stderr timing lines for instrumented UI stages (layer-panel rebuild phases, layer-style dialog open/close, undo snapshots).
- `PATCHY_PERF_SAMPLER=1` (patchy_perf_tests only) samples the main thread's stacks every 10 ms and prints the hottest ones at exit.

Composite checksums from stress reports or large renders are comparable only on the same machine: text antialiasing varies by system and the parallel strip renderer varies with thread count.
