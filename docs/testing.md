# Testing and visual QA

Read this before adding tests, changing test infrastructure, diagnosing suite-only failures, or driving Patchy for visual verification.

## Suite organization

The core suite is one binary split across `tests/core/*_tests.cpp`, one TU per thematic group. Each TU ends with a `<group>_tests()` registration function; `tests/core/main.cpp` concatenates them in a fixed order. Never use static self-registration because cross-TU initialization order would reorder the suite. Append tests to the correct group registration vector. Shared Qt-free helpers live in `tests/core/core_test_support.{hpp,cpp}` and `psd_test_support.{hpp,cpp}` under `namespace patchy::test`; move shared helpers there rather than copying them.

The UI suite follows the same design in `tests/ui/*_tests.cpp`. Registrations are declared in `tests/ui/ui_test_groups.hpp` and concatenated by `tests/ui/main.cpp` in a load-bearing order: contact-sheet and README tests consume artifacts written earlier, and QSettings state intentionally crosses tests. Shared helpers live in `tests/ui/ui_test_support.{hpp,cpp}` under `namespace patchy::test::ui`. `MainWindowTestAccess` in `tests/ui/ui_test_access.hpp` is befriended by its qualified name in `main_window.hpp`.

Groups that outgrew ~3,000 lines are split into part files (`<group>_tests_<theme>.cpp`, each exporting `<group>_tests_partN()`); the original `<group>_tests.cpp` stays as a small aggregator whose exported function concatenates the parts in the original registration order, so the suite order is unchanged. Add a new test to the correct part file's registration vector, keeping the group's overall order intact. Helpers shared by two or more parts of one group live in that group's `<group>_test_support.{hpp,cpp}` (moved, never copied); helpers used by one part stay in that part's anonymous namespace. `patchy_core_tests` has no `/bigobj`, so core part files must stay under ~3,000 lines.

Local-fixture tests skip on remote machines because `local-test-fixtures` is deliberately untracked. The repository-wide fixture sourcing rule lives in `AGENTS.md`.

## Running and filtering

Run `patchy_ui_visual_tests.exe` with `QT_QPA_PLATFORM=offscreen`. Both release test binaries accept a name substring as their first argument. The UI suite also reads `PATCHY_UI_TEST_FILTER`; there is no `--test` flag.

Tests save PNG artifacts through `save_widget_artifact(...)` into `test-artifacts/` beside the binary. Inspect them directly when verifying rendering. Renaming an artifact also requires updating the contact-sheet list in `tests/ui/readme_screenshot_tests_classic.cpp` (the readme_screenshot_tests group is split into part files behind an order-preserving aggregator); stale files in long-lived build directories can otherwise hide the mismatch.

## Offscreen fonts and input

The offscreen platform does not enumerate installed Windows fonts. Register required faces through `tests/test_fonts.hpp` or `QFontDatabase::addApplicationFont`. Never remove an application font during the suite because invalidating an in-use font cache can crash it.

FreeType may expose an OpenType typographic family rather than its familiar GDI family, such as Arial with style Black for `ariblk.ttf`. Use `available_text_family_style_match`; do not gate tests on `QFontDatabase::families().contains(...)`.

Offscreen does not clear `QApplication::keyboardModifiers()` after synthetic key events, and the stuck bit persists in the shared QApplication. Assert behavior through code that reads the current event's folded modifiers. `ui_brush_alt_shows_eyedropper_cursor` is the order-independent reference.

## Failure and lifetime traps

- The test `CHECK()` macro throws. A failure while a MainWindow still owns an open inline text editor can abort during unwind without printing a `[FAIL]` line. Commit or close the editor before assertions that may throw.
- Clicking a layer-row content or mask thumbnail may rebuild and delete the row widget between press and release. Use `click_layer_row_thumbnail(...)`, which refetches the widget for both events; never retain the old pointer.
- If the UI suite dies with an access violation, read the symbolized stack appended by the dbghelp vectored handler in `tests/ui/main.cpp`.
- A crash that occurs only in the full ordered suite is usually an order-dependent heap error. Use the `linux-asan` procedure in [platform.md](platform.md); never reorder or skip tests to conceal it.
- Tests that enable `imports/showPsdWarningsAndInfo` need a repeating QTimer notice dismisser. A one-shot can fire during open progress and leave the suite hung; see [file-formats.md](file-formats.md) under Import notices.
- Platform-specific skips and their reasons are maintained in [platform.md](platform.md).

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
- `PATCHY_UI_TEST_FILTER` selects a UI test substring.

Composite checksums from stress reports or large renders are comparable only on the same machine: text antialiasing varies by system and the parallel strip renderer varies with thread count.
