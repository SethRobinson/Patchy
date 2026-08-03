# Platform notes (macOS/Linux ports)

Deep reference for cross-platform work. Read this before hunting a platform-specific regression, adding a platform-guarded site, or changing packaging/build configuration.

## Cross-platform implementation rules

Windows is the lead platform and must not regress. Every code change still completes the Windows release handoff in `AGENTS.md`. Changes to platform guards, CMake files/presets, or packaging additionally run the affected macOS and/or Linux remote build best-effort and report its result.

- Prefer a small local `#ifdef Q_OS_WIN`, `Q_OS_MACOS`, or `Q_OS_LINUX` (`_WIN32` in Qt-free code) with a portable fallback. Split into `foo_win.cpp`, `foo_mac.mm`, and `foo_linux.cpp` behind `WIN32`, `APPLE`, and `UNIX AND NOT APPLE` only when the site needs Objective-C++/system frameworks or outgrows about one screenful. Per-OS files live beside their feature, not in a platform directory.
- Window-frame code stays in `main_window_chrome.cpp`. `MainWindow::use_custom_window_chrome()` is the only gate: true on Windows for the frameless window, custom title-bar controls, and Qt edge resizing; false on macOS/Linux for native frames. macOS uses the native global menu bar, so never call `setNativeMenuBar(false)` outside gated `configure_window_chrome()` code.
- Tests obtain fonts through `tests/test_fonts.hpp`. Its Windows candidates preserve historical baselines. Triage a macOS/Linux failure by fixing a real bug first, then a platform-specific skip with a reason, then a platform-specific baseline. Never loosen a tolerance globally to make one platform pass.
- File formats remain byte-identical across operating systems. Use explicit endian and fixed-width primitives; never serialize `size_t`, `long`, `wchar_t`, or raw structs.
- Read environment variables through `core/environment.hpp`. The helper returns an owned string so callers never retain process-environment storage, and its Windows implementation avoids the deprecated `getenv` diagnostic.
- Never derive a font size by arithmetic on `pointSizeF()`. A QFont carries its size in either points or pixels and the unused accessor returns -1; macOS resolves inherited widget fonts by pixel size, so `f.setPointSizeF(f.pointSizeF() * 0.85)` asks for -0.85 there. Qt refuses it with `QFont::setPointSizeF: Point size <= 0` and leaves the font unchanged, so text meant to be smaller silently renders at full size on macOS only, and the offscreen suite emits the warning thousands of times. Clamping with `std::max(7.0, ...)` hides the warning and pins the size to the floor, which is the same bug without the diagnostic. Use `scale_font_size` / `scaled_font` / `offset_font` from `dialog_utils.hpp`; they branch on the unit the font actually carries.

## Warning verification

Patchy keeps warnings non-fatal, but supported clean builds must produce no compiler,
linker, or `lrelease` warnings. Build the default target so the application and every
test binary compile.

| Platform | Debug preset | Release preset |
|---|---|---|
| Windows MSVC | `debug` | `release` |
| macOS Clang arm64 | `mac-dev` | `mac-release` |
| Linux GCC x64 | `linux-dev` | `linux-release` |

For warning verification, clean each build directory, configure the preset, build it,
and search the complete combined stdout/stderr log for `warning` and `error`. A clean
Release build on each platform also runs the full core and offscreen UI suites.
Run `patchy_curves_clipping_preview_tests` and `patchy_perf_tests` when validating a
cross-platform cleanup or changing their dependencies.

The remote helper exercises the Release preset:

```powershell
scripts\remote\remote-build.ps1 -Target mac
scripts\remote\remote-build.ps1 -Target linux
```

After it snapshots the tree, use the corresponding remote checkout for clean
`mac-dev` or `linux-dev` verification. Do not add `-Werror` or `/WX`; a new warning is
fixed in source or isolated at the exact vendored source and diagnostic.

## Remote build machinery

macOS (arm64, preset `mac-release`, Qt at `.deps/Qt/6.8.3/macos`) and Linux (preset `linux-release`, Qt at `.deps/Qt/6.8.3/gcc_64`) build remotely via `scripts\remote\remote-build.ps1 -Target mac|linux`, which snapshots the working tree (uncommitted changes included; it creates no commits or branches and does not touch the real index) to a bare repo on `seth@studiomac.local` / `glados@glados.local`, builds there, and runs both suites (core + offscreen UI) with output streamed back. One-time machine provisioning is `scripts/remote/setup-mac.sh` / `setup-linux.sh` (idempotent: venv tools + Qt via aqtinstall + apt deps).

## AddressSanitizer runs (order-dependent heap bugs)

The `linux-asan` preset (RelWithDebInfo + `-fsanitize=address`, own `build/linux-asan` dir) is the tool for crashes that only reproduce in the full ordered suite: it found the July 2026 ~MainWindow teardown and SmartObjectStore reallocation use-after-frees behind the pen-test segfault. Sync the tree with `remote-build.ps1 -Target linux -SkipTests`, then build and run the instrumented suites on the box:

    ssh glados@glados.local "ASAN_OPTIONS='quarantine_size_mb=8192:malloc_context_size=25:detect_leaks=0' \
      bash ~/patchy/src/scripts/remote/build-and-test.sh linux-asan"

The large quarantine keeps long-ago frees poisoned for the whole run (glados has 125 GB RAM); `detect_leaks=0` keeps exits quiet. The UI suite's POSIX SIGSEGV/SIGBUS reporter steps aside under ASAN (`tests/ui/main.cpp`) so sanitizer reports are not preempted. ASAN halts at the first report, so iterate fix-and-rerun until clean. A fresh build dir also surfaces stale `test-artifacts` expectations that long-lived dirs hide; see [testing.md](testing.md).

## Platform-specific site inventory (keep current)

- `main_window_chrome.cpp` + the `use_custom_window_chrome()` call sites in `main_window.cpp` (frameless flag, chrome controls).
- `psd_document_io.cpp` DirectWrite font resolution + wide-string helpers (portable heuristic fallback).
- `layer_list_widget.cpp` drag-wheel low-level mouse hook (degrades gracefully).
- `dialog_utils.cpp` `use_qt_file_dialog_controls` (Qt dialog widgets only under offscreen; native/portal dialogs otherwise, on every OS).
- `dialog_utils_mac.mm` `keep_dialog_above_parent_window` (macOS child-window anchor for non-modal dialogs - see [ui-conventions.md](ui-conventions.md); no-op elsewhere; first Objective-C++ TU, `enable_language(OBJCXX)` is APPLE-gated in CMakeLists).
- Scanner import uses Windows WIA in `scanner_import_win.cpp` and macOS ImageKit/ImageCaptureCore in `scanner_import_mac.mm`; the macOS browser exposes local and network scanners but not cameras, and acquisition remains single-image on both platforms. The AppKit sheet is callback-based so the File menu action returns to the native run loop; never wait for it with a nested `QEventLoop`, which leaves its controls visible but unable to receive mouse input.
- The app stylesheet's `QCheckBox { border: none }` (macOS Aqua layout-item margin suppression - see [ui-conventions.md](ui-conventions.md)) and its APPLE-gated `QGroupBox` block + `brush_dynamics_popup.cpp` `compact_group_grid` (QMacStyle's Aqua group-box chrome and layout spacings blow dense panels past the screen; Windows keeps native metrics).
- `dialog_utils.cpp` `suppress_native_tab_bar_base` (macOS document-mode tab bars paint a light native base across their width even though the `::tab` rules still apply, so the document tabs and Preferences tabs drop the base; no-op elsewhere).
- `main.cpp` `InteractionHintsStyle::styleHint` macOS block (pins SH_FormLayoutFieldGrowthPolicy / LabelAlignment to the Windows behavior because QMacStyle otherwise keeps form fields at size-hint and right-aligns labels, shrinking Name/Folder-style edits to slivers) and the APPLE-gated QScrollBar block in `photoshop_style()` (Windows-classic dithered track via scroll-dither.svg, flat bordered handle, deliberately NO arrow buttons because fixed-size QSS line buttons make the groove degenerate on short scrollbars in collapsed docks; QMacStyle's flat overlay bars hide the handle on the dark theme).
- `main_window_files.cpp` `reveal_path_in_file_explorer`: the Windows branch must pass `/select,` and the file path as SEPARATE QProcess arguments. QProcess quotes any space-containing argument whole, and Explorer reads a quoted `"/select,path"` blob as unparseable, silently opening the default folder (the July 2026 "Reveal in Explorer opens Documents" bug).
- `main_window_files.cpp` `rebuild_recent_files_menu`: the recent-files filter row (a QWidgetAction-hosted line edit) is skipped when `menuBar()->isNativeMenuBar()` is true, because native menu bars (the macOS global bar, Linux appmenu desktops) cannot host widget actions; those platforms keep the plain paged menu. The check is false under `QT_QPA_PLATFORM=offscreen`, so the UI tests exercise the filter row on every platform.
- `update_checker.cpp` platform id (windows/macos/linux manifest keys).
- `main.cpp` Windows app-font candidates + macOS `Contents/Resources` probes (with `localization.cpp`'s translations probe).
- `main_window_palette.cpp` uses `toStdU16String()` for `std::filesystem::path` (UTF-16 -> native on every platform; do not reintroduce `toStdWString`).
- Tests: `test_harness.hpp`, the paired crash reporters in `tests/ui/main.cpp`, `test_fonts.hpp`.

## Color scheme and native chrome

`ThemeManager` mirrors the resolved scheme onto Qt with `QStyleHints::setColorScheme` (`unsetColorScheme` for "follow system"). On Windows that is what re-tints the chrome Patchy does not style: native dialog title bars, dock and list scroll bars, tooltips, `QMessageBox`, and the color picker all follow the app's choice, so there is no `DWMWA_USE_IMMERSIVE_DARK_MODE` call and no registry read. Under `QT_QPA_PLATFORM=offscreen` the call is a no-op and `colorScheme()` reports `Unknown`, which is what keeps the whole UI suite deterministic regardless of the host's Windows setting.

The frameless main window has no native title bar to tint; it draws its own 1px edge from the `window_border` role, which the Light palette overrides explicitly so the window keeps a silhouette against a light desktop. `DWMWA_BORDER_COLOR` in `main_window_chrome.cpp` stays unconditionally `COLOR_NONE`.

## Styled QCheckBox and QMacStyle margins

A stylesheet-styled QCheckBox needs a NON-native border in some matching rule. The app stylesheet's global `QCheckBox { border: none; }` covers this; do not remove it. Qt only suppresses QMacStyle's Aqua layout-item margins (checkboxes: +2,+3,-9,-4) for styled widgets whose rule has a non-native border (qstylesheetstyle.cpp, SE_*LayoutItem). With the margins active, box layouts deliberately overlap the neighboring label ~9px into the checkbox. That is right for the inset native glyph, but on the flat 12px stylesheet indicator the label lands ON the box (the 0.13-mac "text jammed into the checkbox" Layer Style bug). This is only reproducible in the real app: the test harness never loads the QMacStyle plugin, so offscreen/test runs cannot catch a regression here.

## Per-platform test skips (keep this list current)

On macOS/Linux: `ui_bundled_legacy_plugin_action_applies_filter` and `ui_transparency_checkerboard_and_copy_paste_preserve_alpha` (Windows-only bundled legacy 8BF shims; the contact sheet drops their three artifacts), `ui_frameless_window_edges_resize` (native frame owns resize borders; gated on `use_custom_window_chrome()`), and the two `ui_imported_psd_box_text_line_clip_*` tests (they pin Windows Arial line metrics; CoreText/fontconfig lay lines out a few px differently). Seven imported-PSD raster-preview text tests gate on **installed Arial** via `skip_without_arial_for_psd_text_preview()` (Linux ships Liberation, not Arial; without the face the Missing Font prompt correctly appears, which offscreen cannot answer, so the suite HANGS in the nested dialog loop instead of failing). `ui_main_window_renders_color_controls` asserts frameless/badge/window-buttons **presence on Windows and absence elsewhere**. Local-fixture (`local-test-fixtures/`) tests `[SKIP]` on the remotes until the corpus is copied there (sync recipe in [testing.md](testing.md)).

## Remote suites with the fixture corpus present (August 2026, 0.87)

Copying `local-test-fixtures/` to studiomac and glados makes both remotes run the fixture-gated text tests for the first time. Both core suites pass (746 pass, 0 fail), including `composite_corpus_flatten_digests_are_stable` against the **Windows-written** baselines: the CPU compositor is byte-identical across Windows, macOS arm64/clang, Linux x86-64/GCC, and wasm. Do not re-pin those baselines per machine.

The UI suites are not clean, and every failure is a Windows-font assumption in the test, not a product defect:

- **Shared by both remotes** (5): `ui_font_picker_applies_a_pick_the_control_already_shows` (added with the 0.87 style-picker work and never run off Windows), `ui_snes_box_rotated_hscale_commit_matches_if_available`, `ui_restaurant_menu_other_layers_commit_match_if_available`, `ui_restaurant_menu_box_text_edit_commit_keeps_leading_if_available`, `ui_duke_psd_text_runs_survive_reedit`. The four geometry tests compare committed ink/band bounds against tolerances measured on Windows Arial; a substituted face moves them past tolerance.
- **macOS only** (3 more, 919 pass / 8 fail): `ui_text_font_picker_preview_shows_supported_scripts` (takes `QFontDatabase::families(Japanese).front()`, which on a stock macOS is a family the picker deliberately hides, so the row is never found), `ui_dungeon_scroll_psd_text_commit_keeps_placement_if_available`, and `ui_warp_text_render_matches_photoshop_if_available`.
- **`skip_without_arial_for_psd_text_preview()` under-detects on macOS.** Linux ships no Arial and correctly skips seven tests; macOS carries `ArialHB`/`Arial Unicode`, which satisfies the check, so those tests run against metrics that are not Windows Arial's and fail. Widen the guard to require the real Arial family, not any Arial-named face.
- **Linux hangs on `ui_dungeon_scroll_psd_text_commit_keeps_placement_if_available`** (921 pass / 5 fail / 1 hang). This is the documented offscreen trap: the fixture's face is absent, the Missing Font prompt opens, and nothing offscreen can answer it. macOS fails this test instead of hanging. Until it is gated, a Linux UI run with the corpus present needs the hung test excluded; the rest of the suite runs green when resumed past it via `PATCHY_UI_TEST_FILTER`.

## macOS non-modal dialog anchoring

On macOS, `run_non_modal_dialog` anchors the dialog as a native child window of its parent widget's window (`keep_dialog_above_parent_window`, dialog_utils_mac.mm): macOS has no Win32 owned-window z-order, so clicking the edit-locked main window buried the dialog behind it and the app looked frozen (0.13 mac bug). The anchor attaches on Show (deferred one event-loop turn) and MUST detach on Hide/Close because AppKit re-orders attached children with their parent even when hidden. Child windows follow parent moves; that is accepted mac-native behavior. Any new non-modal dialog path that bypasses `run_non_modal_dialog` needs the same call.
