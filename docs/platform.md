# Platform notes (macOS/Linux ports)

Deep reference for cross-platform work. Read this before hunting a platform-specific regression, adding a platform-guarded site, or changing packaging/build configuration.

## Cross-platform implementation rules

Windows is the lead platform and must not regress. Every code change still completes the Windows release handoff in `AGENTS.md`. Changes to platform guards, CMake files/presets, or packaging additionally run the affected macOS and/or Linux remote build best-effort and report its result.

- Prefer a small local `#ifdef Q_OS_WIN`, `Q_OS_MACOS`, or `Q_OS_LINUX` (`_WIN32` in Qt-free code) with a portable fallback. Split into `foo_win.cpp`, `foo_mac.mm`, and `foo_linux.cpp` behind `WIN32`, `APPLE`, and `UNIX AND NOT APPLE` only when the site needs Objective-C++/system frameworks or outgrows about one screenful. Per-OS files live beside their feature, not in a platform directory.
- Window-frame code stays in `main_window_chrome.cpp`. `MainWindow::use_custom_window_chrome()` is the only gate: true on Windows for the frameless window, custom title-bar controls, and Qt edge resizing; false on macOS/Linux for native frames. macOS uses the native global menu bar, so never call `setNativeMenuBar(false)` outside gated `configure_window_chrome()` code.
- Tests obtain fonts through `tests/test_fonts.hpp`. Its Windows candidates preserve historical baselines. Triage a macOS/Linux failure by fixing a real bug first, then a platform-specific skip with a reason, then a platform-specific baseline. Never loosen a tolerance globally to make one platform pass.
- File formats remain byte-identical across operating systems. Use explicit endian and fixed-width primitives; never serialize `size_t`, `long`, `wchar_t`, or raw structs.

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
- `dialog_utils.cpp` `suppress_native_tab_bar_base` (macOS document-mode tab bars paint a light native base across their width — the ::tab rules still apply — so the document tabs and Preferences tabs drop the base; no-op elsewhere).
- `main.cpp` `InteractionHintsStyle::styleHint` macOS block (pins SH_FormLayoutFieldGrowthPolicy / LabelAlignment to the Windows behavior — QMacStyle otherwise keeps form fields at size-hint and right-aligns labels, shrinking Name/Folder-style edits to slivers) and the APPLE-gated QScrollBar block in `photoshop_style()` (Windows-classic dithered track via scroll-dither.svg, flat bordered handle, deliberately NO arrow buttons — fixed-size QSS line buttons make the groove degenerate on short scrollbars in collapsed docks; QMacStyle's flat overlay bars hide the handle on the dark theme).
- `main_window_files.cpp` `reveal_path_in_file_explorer`: the Windows branch must pass `/select,` and the file path as SEPARATE QProcess arguments. QProcess quotes any space-containing argument whole, and Explorer reads a quoted `"/select,path"` blob as unparseable, silently opening the default folder (the July 2026 "Reveal in Explorer opens Documents" bug).
- `update_checker.cpp` platform id (windows/macos/linux manifest keys).
- `main.cpp` Windows app-font candidates + macOS `Contents/Resources` probes (with `localization.cpp`'s translations probe).
- `main_window_palette.cpp` uses `toStdU16String()` for `std::filesystem::path` (UTF-16 -> native on every platform — do not reintroduce `toStdWString`).
- Tests: `test_harness.hpp`, the paired crash reporters in `tests/ui/main.cpp`, `test_fonts.hpp`.

## Styled QCheckBox and QMacStyle margins

A stylesheet-styled QCheckBox needs a NON-native border in some matching rule — the app stylesheet's global `QCheckBox { border: none; }` covers this; do not remove it. Qt only suppresses QMacStyle's Aqua layout-item margins (checkboxes: +2,+3,-9,-4) for styled widgets whose rule has a non-native border (qstylesheetstyle.cpp, SE_*LayoutItem). With the margins active, box layouts deliberately overlap the neighboring label ~9px into the checkbox — right for the inset native glyph, but on the flat 12px stylesheet indicator the label lands ON the box (the 0.13-mac "text jammed into the checkbox" Layer Style bug). Only reproducible in the real app: the test harness never loads the QMacStyle plugin, so offscreen/test runs cannot catch a regression here.

## Per-platform test skips (keep this list current)

On macOS/Linux: `ui_bundled_legacy_plugin_action_applies_filter` and `ui_transparency_checkerboard_and_copy_paste_preserve_alpha` (Windows-only bundled legacy 8BF shims; the contact sheet drops their three artifacts), `ui_frameless_window_edges_resize` (native frame owns resize borders; gated on `use_custom_window_chrome()`), and the two `ui_imported_psd_box_text_line_clip_*` tests (they pin Windows Arial line metrics; CoreText/fontconfig lay lines out a few px differently). Seven imported-PSD raster-preview text tests gate on **installed Arial** via `skip_without_arial_for_psd_text_preview()` (Linux ships Liberation, not Arial; without the face the Missing Font prompt correctly appears, which offscreen cannot answer — the suite HANGS in the nested dialog loop, it does not fail). `ui_main_window_renders_color_controls` asserts frameless/badge/window-buttons **presence on Windows and absence elsewhere**. Local-fixture (`local-test-fixtures/`) tests `[SKIP]` on the remotes because that directory is deliberately untracked.

## macOS non-modal dialog anchoring

On macOS, `run_non_modal_dialog` anchors the dialog as a native child window of its parent widget's window (`keep_dialog_above_parent_window`, dialog_utils_mac.mm): macOS has no Win32 owned-window z-order, so clicking the edit-locked main window buried the dialog behind it and the app looked frozen (0.13 mac bug). The anchor attaches on Show (deferred one event-loop turn) and MUST detach on Hide/Close — AppKit re-orders attached children with their parent even when hidden. Child windows follow parent moves; that is accepted mac-native behavior. Any new non-modal dialog path that bypasses `run_non_modal_dialog` needs the same call.
