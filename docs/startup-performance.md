# Startup performance: phase trace, benchmark, regression check

Read before changing anything on the startup path (`src/app/main.cpp`, the `MainWindow` constructor, work queued for the first event-loop turns) or the startup timing tools. The rendering rules live in [performance.md](performance.md).

## The phase trace

`support/startup_trace.{hpp,cpp}` (Qt-free, in `patchy_core`) records named phases on a monotonic clock whose zero is the OS process creation time (`GetProcessTimes` on Windows, `kinfo_proc.p_starttime` on macOS, `/proc/self/stat` field 22 on Linux, 10 ms resolution). The span from creation to `main()` is the synthetic phase `process.pre_main`: the loader, DLL mapping (Qt and every import), and static initializers. Where the start is unknown (wasm) times count from `main()` and the report says `process_start_known: false`.

- **Switches.** `PATCHY_STARTUP_TRACE=1` prints the table to stderr once startup is idle (a GUI-subsystem exe has no console: redirect, `patchy.exe 2> trace.txt`). `--startup-benchmark <report.json>` (hidden from `--help`; read from argv before the QApplication by `startup_benchmark_flag_value`) writes the JSON report and exits 0, or 3 when the report could not be written or startup never went idle within two minutes. It never forwards to a running instance, combines with files to open (startup with a document) and with `--headless`, and refuses `--stress-test`, `--export`, `--run-script` and `--screenshot` (exit 2).
- **Zero cost when off.** `initialize(false)` leaves every `Scope`/`mark` a test of a plain bool; the observer (event filter, `aboutToBlock` hook, report) is only created when tracing. After the report, `stop()` turns recording off again.
- **Names are identifiers.** Baselines key on phase names: never rename one; add a new name. Record from the GUI thread only; names are string literals (stored as pointers).
- **What "ready" means.** `first_paint` is the first `QEvent::Paint` of a widget in the main window. `startup.idle` is the first `QAbstractEventDispatcher::aboutToBlock` after that paint once `app.exec()` runs (a zero timer queued just before exec raises `event_loop.enter`; nested loops before exec, such as a command-line open's progress, never count). At that point the window has drawn and every zero-timer task queued during startup (the plug-in scan kick-off, the recent-file check) has run. The JSON's `ready_ms` is that mark.
- **Background work.** The legacy plug-in scan (Windows) runs on a worker after the window is up; its whole span (`plugins.legacy_scan`) and the GUI-thread part that applies the result (`plugins.legacy_scan_apply`) are reported as background. The report waits up to 20 s for it.
- **Benchmark-mode differences.** The orphaned-recovery step only lists other instances' recovery folders (`list_orphaned_recovery`, timed as `recovery.orphan_scan`) instead of adopting them. `--headless` adds what every headless run has: offscreen platform, CLI automation mode, no MCP attachment listener, and no installed-font enumeration (the offscreen platform uses FreeType and never enumerates Windows fonts, see [testing.md](testing.md)), so `fonts.ui_family` and anything text-heavy are only representative in a real window.

Phases, in order (`mw.*` are children of `main_window.construct`):

| Phase | Covers |
|---|---|
| `settings.gui_scale` | first `app_settings()` read (ini parse) for the interface scale |
| `qt.application` | `QApplication` construction: platform plugin, screens, input context |
| `app.style_identity` | `AppStyle`, names, window icon (SVG render), allocation limit |
| `fonts.bundled` | `addApplicationFont` for the bundled fonts |
| `app_data.migration` | 0.98-era app-data move check |
| `fonts.user_store` | user font store: SHA-256 and registration of every file, PSD font resolver |
| `localization.load` | saved language and its translators, `--language` |
| `fonts.ui_family` | `application_font()`: on Windows `QFontDatabase::families()` (full font enumeration in a real window) |
| `fonts.headless_system` | `--headless` on Windows (not `--startup-benchmark`): every installed font registered, so headless text resolves as in a window |
| `theme.load` | saved color scheme and custom theme |
| `cli.parse` | option table and parse |
| `single_instance.forward_probe` | connect attempt to a running instance |
| `main_window.construct` | the whole constructor; children below |
| `mw.recovery_start` | status bar class, recovery coordinator |
| `mw.filters_codecs` | built-in filter registry, ICO/RTTEX codecs |
| `mw.tabs_start_panel` | document tabs, start panel, timers |
| `mw.input_color_settings` | pen settings, CMYK working space (ICC load) |
| `mw.actions` | `create_actions`; children `mw.actions.defaults_canvas`, `.menu_bar`, `.tool_palette`, `.options_bar`, `.translations_finish` |
| `mw.lib.*` | a preset library built on first use (`brush_tips`, `brush_presets`, `patterns`, `gradients`, `styles`, `custom_shapes`): nested under whatever phase first touched it, today `mw.actions.options_bar`; `mw.lib.patterns` is the adoption of the startup preload, usually after `first_paint` |
| `mw.view_recent` | view settings, window chrome, recent files and folders menus |
| `mw.docks` | `create_docks` (every panel) |
| `mw.panel_entries_hotkeys` | Window menu panel entries, hotkey application |
| `mw.scripts` | bundled and user script scan for script hotkeys |
| `mw.initial_refresh` | empty layer list and action state |
| `mw.geometry_layout` | title, saved geometry, panel layout restore |
| `mw.stylesheet` | `setStyleSheet(photoshop_style())` over the whole widget tree |
| `mw.status_bar` | status bar widgets and zoom controls |
| `single_instance.listen` | the second-launch server |
| `main_window.show` | polish, layout, native window creation |
| `update_check.start` | update-check setup (the request itself is asynchronous) |
| `recovery.orphan_scan` | crashed-instance recovery scan |
| `cli.open_files` | command-line documents, when given |
| `mcp.attachment` | `--attach` listener thread start (blocks until listening) |
| `appimage.menu_entry` | AppImage only: refresh of the applications menu entry, or scheduling its one-time offer ([appimage.md](appimage.md)) |

## Measuring: `patchy_perf_tests startup`

The startup scenario of the existing perf harness (`tests/perf_startup.cpp`; explicit only, never part of the default perf run) launches the sibling `patchy` executable with `--startup-benchmark`, once on a fresh profile (reported alone as "first"), then N times warm, and prints median, minimum and MAD (scaled to a standard deviation) per phase. The target depends on `patchy`, so building the perf tests builds the app.

- Every launch is isolated: `PATCHY_SETTINGS_DIR`, `PATCHY_USER_FONTS_DIR`, `PATCHY_RECOVERY_DIR` and `PATCHY_MCP_ENDPOINT` point into `test-artifacts/perf-startup/` (cleared at the start of each run; reports `first-0.json`, `warm-<i>.json`, `cold-<i>.json` stay there for inspection), plus `PATCHY_NO_SINGLE_INSTANCE`, `PATCHY_NO_SOUND`, and `PATCHY_NO_UPDATE_CHECK` (no network per launch). The profile persists across the run's launches, so warm launches see an existing profile.
- Knobs: `PATCHY_PERF_STARTUP_RUNS` (10), `PATCHY_PERF_ONSCREEN=1` (a real window instead of `--headless`), `PATCHY_PERF_STARTUP_UPDATE_CHECK=1` (keep the update check), `PATCHY_PERF_STARTUP_EXE` (another build), `PATCHY_PERF_STARTUP_COLD=1` with `PATCHY_PERF_STARTUP_COLD_RUNS` (5), `PATCHY_PERF_STARTUP_FLOOR_MS` (10).
- Output: the table on stdout and `test-artifacts/perf-startup-<headless|window>.json` (`patchy-startup-summary`: first, warm, cold stats per metric key `ready`, `pre_main`, `phase:<name>`, `mark:<name>`, `background:<name>`).
- **Warm** means the exe, Qt DLLs, plug-ins and fonts are in the file cache from the previous launch. **Cold** (Windows) flushes the modified page list and purges the standby list (`NtSetSystemInformation(SystemMemoryListInformation, MemoryFlushModifiedList / MemoryPurgeStandbyList)`, what RAMMap's Empty Standby List does) before each launch, then waits 1.5 s. It needs an elevated process (`SeProfileSingleProcessPrivilege`; the offload hosts' ssh sessions are elevated) and otherwise prints `[SKIP]` and measures warm only. It is an approximation of a first launch after boot, not the same thing: files mapped by running processes (system DLLs) stay resident, the Windows font cache service keeps its data, and the prefetcher (SysMain) may read Patchy's files back from its trace as it would after a boot. Defender scans the freshly read files as it does for a user. Linux and macOS cold runs are not implemented (`drop_caches` and `purge` need root).
- The perf harness runs through `run-throttled.bat` (below-normal CPU priority, inherited by the launches); that only matters when something else competes for the CPU, so measure on an idle machine.

## The regression check

The scenario compares its medians with a reference and fails (exit 1) on a regression:

- **Per-host baseline.** `local-test-fixtures/startup-baselines/<host>-<mode>.json` (gitignored: machine names never enter tracked files), or `PATCHY_PERF_STARTUP_BASELINE=<file>`. `PATCHY_PERF_STARTUP_RECORD=1` writes the current summary there instead of comparing. Record on a known-good build; re-record after a deliberate startup change. A baseline from another CPU is used with a warning.
- **A/B against another build.** `PATCHY_PERF_STARTUP_COMPARE_EXE=<old patchy.exe>` interleaves launches of both builds (A, B, A, B), so machine drift hits both, and needs no stored baseline. This is the most noise-robust check.
- **Margins.** Total (`ready`): WARN above 1.10x and +30 ms, FAIL above 1.25x and +60 ms. A phase (median at least the 10 ms floor in the reference): WARN above 1.25x and +10 ms, FAIL above 1.50x and +25 ms. Every margin also requires the slowdown to exceed three times the larger MAD of the two sets. Marks and background spans are reported, never judged. `PATCHY_PERF_STARTUP_WARN_ONLY=1` turns a FAIL into a warning.

On an offload host: `scripts\remote\remote-build.ps1 -Target windows -WindowsHost <host> -SkipTests -PerfScenario startup -PerfEnv 'PATCHY_PERF_STARTUP_COLD=1' -FetchArtifacts` (`-PerfEnv` takes `NAME=value;NAME=value`; with `-BuildTarget` the scenario runs only when `patchy_perf_tests` is among the targets, which builds `patchy` too). The release preset is the one to measure: `opt-dev` has no LTCG and times differently. Those hosts run over ssh in session 0, which has no interactive desktop: a real window never paints there, so window mode prints `[SKIP] startup window: ... session 0` and only headless numbers come from them. A launch that fails says which of `event_loop.enter`, `first_paint`, `startup.idle` it never reached.

### Real window by hand (a desktop session)

From the release build directory in a normal (not elevated, unless measuring cold) terminal, with no other build or app competing:

```powershell
$env:PATCHY_PERF_ONSCREEN=1; .\patchy_perf_tests.exe startup
```

Windows open and close by themselves (about one per second). For a cold real-window number, run that terminal elevated and add `$env:PATCHY_PERF_STARTUP_COLD=1`. For one interactive launch with the table: `$env:PATCHY_STARTUP_TRACE=1; .\patchy.exe 2> startup-trace.txt`, wait for the window, close Patchy, read the file.

## Baseline: an offload host, October 2026

i7-1360P laptop (16 threads), Windows 11 26H2, release preset (LTCG), Qt 6.8.3, headless, update check off, no Defender exclusion for the build folder. Medians in ms; the host keeps them in its `local-test-fixtures/startup-baselines/`.

| | ready (`startup.idle`) | `process.pre_main` | `main_window.construct` |
|---|---|---|---|
| warm, 10 launches (two runs) | 330 (min 319, MAD 3), 333 | 16 | 281 |
| cold, 5 launches (two runs) | 393 and 429 | 78 to 88 | 278 to 313 |
| first launch, fresh profile | 2771 to 2866 | 20 to 63 | 2704 to 2742 |

Cold costs 60 to 100 ms over warm, almost all in `pre_main` (mapping Qt and the other imported DLLs) plus slower first reads in the preset libraries. The window paints about 1 ms after `event_loop.enter`; `first_paint` to `startup.idle` (the first frame and the queued startup tasks) is about 13 ms.

### Slowest phases (warm headless) and proposals

Rows marked Done are implemented; the rest are proposals to pick from.

| Rank | Phase | Warm | First launch | Proposal and risk |
|---|---|---|---|---|
| 1 | `mw.lib.patterns` | 154 ms (47% of ready) | 2466 ms | **Done** (see "Preloaded libraries" below). Was: the Pattern Stamp combo built at startup made `PatternLibrary::reload` decode every stored pattern (32 files, 10 MB, mostly 512x512), and a fresh profile first generated and PNG-encoded the factory set. |
| 2 | `mw.stylesheet` | 38 ms | 40 ms | `setStyleSheet(photoshop_style())` runs after every widget exists, so the whole tree is re-polished once. Setting the sheet before `create_actions`/`create_docks` (widgets are then polished once, when shown) may save part of it. Risk: code that reads style metrics during construction would see the styled values earlier, and the scheme-change connection order (connected after the first sheet) must stay. Measure before keeping. |
| 3 | `mw.lib.brush_presets` (includes `mw.lib.brush_tips`, 10 ms) | 23 ms | 141 ms | **Done** (see "Preloaded libraries" below). Was: every tip PNG decoded into a 48 px thumbnail, then decoded again by the brush preset library's first refresh, and a fresh profile seeded the default tips on the GUI thread. |
| 4 | `process.pre_main` | 16 ms | cold 78 to 88 ms | The exe names Qt6Pdf, Qt6Qml, Qt6Network, Qt6PrintSupport, Qt6Svg and onnxruntime (a string scan; confirm with `dumpbin /imports`). Delay-load (`/DELAYLOAD`) what startup never calls (likely Qt6Pdf, onnxruntime, Qt6PrintSupport). Risk: a DLL whose data Patchy imports (a `staticMetaObject` of a subclassed class) cannot be delay-loaded (the linker refuses, LNK1194), and a missing DLL then fails at first use instead of at launch. Helps cold starts only. |
| 5 | rest of `mw.actions.options_bar` | 18 ms | | The options-bar widgets themselves. Building tool pages on first tool switch would save most of it; risk: hotkeys, translations and tests that reach the hidden pages by object name. |
| 6 | first frame and queued tasks | 13 ms | | `first_paint` to `startup.idle`; includes the plug-in scan kick-off and the recent-file check. Already after the window is up. |
| 7 | `mw.scripts` | 10 ms | 11 ms | The script scan reads every bundled and user script to register script hotkeys. Cache the scan by directory and file times, or scan on a worker and register when it returns. Risk: a script hotkey pressed in the first moments would be missed. |
| 8 | everything else | 5 ms or less each | | `mw.tabs_start_panel`, `mw.input_color_settings` (ICC working space), `mw.view_recent`, `mw.docks`, `mw.actions.translations_finish`, `main_window.show`, `mw.actions.menu_bar`. |

Off the startup path already: the legacy plug-in scan (14 ms, on a worker after the first paint, reported as background), the update check (asynchronous; host lookup on a detached thread), G'MIC (its library loads at the first filter run, not at startup). Not measured: a real window. Headless never enumerates installed fonts, so `fonts.ui_family` (about 1 ms here) can be much larger in a real window on a machine with many fonts; measure it with the desktop procedure above.

## Preloaded libraries

`MainWindow::start_library_preloads()`, the first statement of the constructor, starts tracked workers that read, decode and seed the brush tips and the patterns while the window is built (`BrushTipLibrary::load_off_gui_thread`, `PatternLibrary::load_off_gui_thread`). A worker-side library keeps its thumbnails as `QImage`s (QPixmap is GUI-thread only; `brush_tip_thumbnail_image`, `pattern_thumbnail_image`), and the GUI adopts the result with `from_load()`, which reads nothing. The options bar needs the tips about 10 ms after the workers start, so their adoption usually waits briefly (a fresh profile's seeding most of all); the patterns are adopted after the first paint. Rules:

- Every reader goes through the window getters (`brush_tip_library()`, `pattern_library()`), which adopts the preload and waits for the worker when it is still running, so an early reader (a document opened at once, a script, the MCP connector) gets the complete library, never a partial one. Never read a library folder directly while a preload may still be writing it.
- The seeding version gates are read on the GUI thread before the worker starts and written back after adoption, as the synchronous path did; adoption emits the library's `changed` when the worker seeded, like the seeding itself did.
- The Pattern Stamp list stays empty until the library exists: the pattern worker posts its adoption to the GUI as soon as it finishes (`adopt_preloaded_pattern_library`), and the first click, key or wheel on the list adopts it at once (`FirstUseHook`). A language change adopts it first (`retranslate_ui`), because the list is rebuilt with translated names and a switch made before the worker lands must still list every pattern; the build-time translation check (`ui_translation_language_switch_matches_fresh_windows`, a fresh profile whose first worker seeds the factory set) compares a switched window against a fresh one and failed on a slow Linux host before this. `ui_pattern_stamp_list_fills_on_language_switch_before_preload_lands` pins it through the test seam `MainWindowTestAccess::hold_pattern_preload`, which keeps a window's pattern worker from publishing until the window asks for the library, is destroyed, or the hold is cleared.
- A window destroyed before adopting waits for its workers (`~StartupLibraryPreloads`), so no worker keeps writing into a settings folder a test is about to delete.
- `BrushAutomationLibrary`'s first refresh no longer reloads the tip library it was handed when the folder matches what that library read (`loaded_signature`, the same `preset_storage_signature` the 1 s watcher compares); later changes by another process still reload it.
- `ui_pattern_library_off_thread_load_matches_direct_load` and `ui_brush_tip_library_off_thread_load_matches_direct_load` pin that an adopted library equals the GUI-thread one (entries, thumbnails, tiles or masks), that the worker's files read back the same, and (brushes) that the preset library's first refresh reloads nothing while an outside edit still does.

Measured on the baseline host (medians, headless, against a baseline recorded from the same day's main):

| Change | Phase before -> after | ready warm | ready cold | first launch |
|---|---|---|---|---|
| Patterns on a worker | `mw.lib.patterns` 152.7 -> 0.5 ms warm (adoption after the first paint), 153.1 -> 0.8 ms cold; `mw.actions.options_bar` 193.2 -> 40.0 | 328.5 -> 177.8 ms | 419.6 -> 352.7 ms (MAD 40) | 2838 -> 397 ms |
| Brush tips on a worker, no second tip read | `mw.lib.brush_presets` 22.4 -> 3.8 ms warm (33.5 -> 4.0 cold), its child `mw.lib.brush_tips` 9.8 -> 0.2 ms; `mw.actions.options_bar` 40.0 -> 20.5; the pattern adoption now lands after the report | 177.8 -> 161.4 ms | 352.7 -> 258.4 ms (MAD 15) | 397 -> 301 ms |

Cold launches vary by 50 ms or more from disk and page-fault noise (every phase on the path still drops by the same 150 ms); read cold medians with their MAD.
