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

### The flatpak build is a second Linux compiler

A warning reported by the flatpak release build usually cannot be reproduced or
verified by `remote-build.ps1 -Target linux`. The two use different compilers and
different flags: `linux-release` on glados is the system `/usr/bin/c++` (GCC 13.3.0 on
Ubuntu 24.04) with only `-O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic`, while
`packaging/linux/make-flatpak.sh` builds inside `org.kde.Sdk//6.8` with GCC 14.3.0 and
flatpak-builder's hardening flags (`-Wp,-D_FORTIFY_SOURCE=3 -Wp,-D_GLIBCXX_ASSERTIONS
-fstack-protector-strong -fstack-clash-protection -fcf-protection -fno-omit-frame-pointer`,
then `-O3 -DNDEBUG` on top). Both the newer libstdc++ headers and `_GLIBCXX_ASSERTIONS`
change inlining enough to produce optimizer diagnostics the plain preset never emits.
The August 2026 `-Wfree-nonheap-object` and `-Warray-bounds=` false positives in
`psd_filter_effects.cpp` and `af_document_io.cpp` were both invisible to the preset
build and both reproduced under these flags.

Checking a single translation unit is far cheaper than a full flatpak run. Take the
object's `FLAGS` and `INCLUDES` from a previous flatpak build tree
(`packaging/linux/.flatpak-builder/build/patchy-*/build.ninja`, which survives
`--force-clean`) and compile it with the SDK compiler:

    flatpak run --filesystem=home --command=g++ org.kde.Sdk//6.8 <INCLUDES> <FLAGS> \
      -c ~/patchy/src/src/formats/af_document_io.cpp -o /dev/null

Compiling a copy of the pre-fix file the same way confirms a fix actually removed the
warning instead of merely being compiled by something that never reported it.

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
- `dialog_utils.cpp` `use_qt_file_dialog_controls` (Qt dialog widgets only under offscreen; native/portal dialogs otherwise, on every OS). The Print dialog's "Print Using System Dialog..." has no such switch: it is always the platform dialog, so the UI test only checks the button, never clicks it.
- The Print dialog's "Print Using System Dialog..." is `QPrintDialog` on every desktop OS. On Windows 11 22H2+ that is the "unified" `PrintDlgEx` dialog, which shows "This app doesn't support print preview" to every Win32 caller (only WinRT `PrintManager` apps get a preview there); Chrome's system dialog gets the same. Do not switch it to the classic `PrintDlg` + `PD_ENABLEPRINTHOOK` dialog again: that was tried in August 2026 because of a `0x80070002` error that turned out to be a broken Microsoft Print to PDF install (system-wide, reproduced in Photoshop), and the classic dialog only hides the driver's options behind Properties....
- `print_dialog.cpp` `ensure_printer_driver_usable` / `windows_printer_device_is_usable` (Windows only): Qt 6 assumes every printer has a DEVMODE, but `QWin32PrintEnginePrivate::initialize` calls `release()` (which nulls it) when `CreateDC` fails, and `QPageSetupDialog::exec` then dereferences the null pointer and takes the app down. A Microsoft Print to PDF whose port registration is broken reproduces this: `CreateDC` fails with `ERROR_PATH_NOT_FOUND` while the spooler still hands out a DEVMODE, so the guard tests the device context rather than the DEVMODE, and Print, Page Setup, and the system dialog report "Printer unavailable" instead of crashing (August 2026 crash dumps).
- `dialog_utils_mac.mm` `keep_dialog_above_parent_window` (macOS child-window anchor for non-modal dialogs - see [ui-conventions.md](ui-conventions.md); no-op elsewhere; first Objective-C++ TU, `enable_language(OBJCXX)` is APPLE-gated in CMakeLists).
- Scanner import uses Windows WIA in `scanner_import_win.cpp` and macOS ImageKit/ImageCaptureCore in `scanner_import_mac.mm`; the macOS browser exposes local and network scanners but not cameras, and acquisition remains single-image on both platforms. The AppKit sheet is callback-based so the File menu action returns to the native run loop; never wait for it with a nested `QEventLoop`, which leaves its controls visible but unable to receive mouse input.
- The app stylesheet's `QCheckBox { border: none }` (macOS Aqua layout-item margin suppression - see [ui-conventions.md](ui-conventions.md)) and its APPLE-gated `QGroupBox` block + `brush_dynamics_popup.cpp` `compact_group_grid` (QMacStyle's Aqua group-box chrome and layout spacings blow dense panels past the screen; Windows keeps native metrics).
- `dialog_utils.cpp` `suppress_native_tab_bar_base` (macOS document-mode tab bars paint a light native base across their width even though the `::tab` rules still apply, so the document tabs and Preferences tabs drop the base; no-op elsewhere).
- `main.cpp` `InteractionHintsStyle::styleHint` macOS block (pins SH_FormLayoutFieldGrowthPolicy / LabelAlignment to the Windows behavior because QMacStyle otherwise keeps form fields at size-hint and right-aligns labels, shrinking Name/Folder-style edits to slivers) and the APPLE-gated QScrollBar block in `photoshop_style()` (Windows-classic dithered track via scroll-dither.svg, flat bordered handle, deliberately NO arrow buttons because fixed-size QSS line buttons make the groove degenerate on short scrollbars in collapsed docks; QMacStyle's flat overlay bars hide the handle on the dark theme).
- `main_window_files.cpp` `reveal_path_in_file_explorer`: the Windows branch must pass `/select,` and the file path as SEPARATE QProcess arguments. QProcess quotes any space-containing argument whole, and Explorer reads a quoted `"/select,path"` blob as unparseable, silently opening the default folder (the July 2026 "Reveal in Explorer opens Documents" bug).
- `main_window_files.cpp` `rebuild_recent_files_menu`: the recent-files filter row (a QWidgetAction-hosted line edit) is skipped when `menuBar()->isNativeMenuBar()` is true, because native menu bars (the macOS global bar, Linux appmenu desktops) cannot host widget actions; those platforms keep the plain paged menu. The check is false under `QT_QPA_PLATFORM=offscreen`, so the UI tests exercise the filter row on every platform.
- Optional Qt add-ons get their OWN `find_package(Qt6 QUIET COMPONENTS <X>)` and a `if(TARGET Qt6::X)` source swap, never a slot in `PATCHY_QT_COMPONENTS`: that lookup is QUIET, so one missing component silently skips the entire app target. Qt PDF is the current example (`pdf_import.cpp` vs `pdf_import_stub.cpp`, `PATCHY_HAVE_QT_PDF`); QtPrintSupport uses the same swap shape but keyed on EMSCRIPTEN because Qt simply publishes no wasm build of it. Both stubs must keep every entry point real enough to report the feature as unavailable.
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

On macOS/Linux: `ui_bundled_legacy_plugin_action_applies_filter` and `ui_transparency_checkerboard_and_copy_paste_preserve_alpha` (Windows-only bundled legacy 8BF shims; the contact sheet drops their three artifacts), `ui_frameless_window_edges_resize` (native frame owns resize borders; gated on `use_custom_window_chrome()`), and the two `ui_imported_psd_box_text_line_clip_*` tests (they pin Windows Arial line metrics; CoreText/fontconfig lay lines out a few px differently). `ui_main_window_renders_color_controls` asserts frameless/badge/window-buttons **presence on Windows and absence elsewhere**. Local-fixture (`local-test-fixtures/`) tests `[SKIP]` on the remotes until the corpus is copied there (sync recipe in [testing.md](testing.md)).

Text tests additionally gate on the fixture's own face. All three guards live in `tests/ui/ui_test_support.cpp`, and all three exist for the same two reasons: a substituted face turns a pinned geometry tolerance into a measurement of the substitute, and entering a text session without the face raises the Missing Font prompt, which nothing can answer under offscreen (the suite HANGS in the nested dialog loop instead of failing).

- `skip_without_arial_for_psd_text_preview()` gates seven imported-PSD raster-preview text tests, the three `ui_af_*` text tests, and `ui_warp_text_render_matches_photoshop_if_available`. macOS ships genuine Arial in `/System/Library/Fonts/Supplemental` and runs all eleven green; Linux ships Liberation, not Arial, and skips them.
- `skip_without_font_face(family, role)` gates on a face being reachable at all, matching MainWindow's family-or-`"family style"` split (Qt files Arial Black under family Arial, style Black). Candara (`restaurant-menu-inside.psd`) and Bookman Old Style (`dungeon-scroll-game-screen.psd`) are Windows-only; Georgia is Windows and macOS.
- `skip_without_psd_text_face(layer, family)` adds the second way a machine lands off the Windows baseline: the family the imported layer actually resolved to. PSD PostScript names go through DirectWrite on Windows and the Qt font database elsewhere (`install_font_database_psd_font_resolver` in `src/ui/psd_font_resolver.hpp`, installed by the app and the UI test main; the suffix-stripping heuristic in `psd_text_read.cpp` is the last resort, and wasm stays heuristic-only so bundled alias families are never baked into imported metadata). The database resolver handles both shapes a face can take off Windows, whole family ("Arial Black") and family "Arial" + style "Black", mirroring the DirectWrite output rules (flag-expressible faces stay family + flags, mid weights keep the real face without the bold flag, Black/Heavy keeps the full name plus bold). The guard therefore fires only when the face was absent from the database at read time: the `snes-box-a3.psd` probes run on Windows and macOS and skip on Linux, which has no Arial Black. Tests must register a fixture's face BEFORE reading the PSD.

## Remote suites with the fixture corpus present (August 2026, 0.87)

`local-test-fixtures/` is copied to studiomac and glados, so both remotes run the fixture-gated text tests. All four suites pass: both core suites, and the UI suite with no failures on each remote (a `[SKIP]` line reports everything a machine cannot cover: as of the 0.87 font-database resolver work, Linux prints 30 platform skips, macOS 13, Windows 0, plus one fixture-missing line per machine for each `local-test-fixtures` PSD a test names that is not present -- currently `akiko_cycling_okinawa_with_filters.psd` and `eon_spider.psd` everywhere, this machine included). The core result includes `composite_corpus_flatten_digests_are_stable` against the **Windows-written** baselines: the CPU compositor is byte-identical across Windows, macOS arm64/clang, Linux x86-64/GCC, and wasm. Do not re-pin those baselines per machine.

What the corpus exposed was Windows-font assumptions in the tests, not product defects. Most are now the face gates listed above; three tests needed something else:

- `ui_warp_text_render_matches_photoshop_if_available` (macOS only - Linux has no Arial) compares Patchy's warp against Photoshop PNGs of Windows Arial. CoreText costs ~0.06 of IoU and ~2px of ink extent on the point-text cases without moving the warp geometry, so off Windows the IoU floors scale by 0.90 and the bounds tolerance gains 2px. Every case reports its numbers before the test fails, so one run shows the whole picture.
- `ui_duke_psd_text_runs_survive_reedit` reflows `Duke nukem mobile.psd`'s body between the first and second apply. Its face (FuturaLT-ExtraBold) is installed nowhere, so what reflows is each platform's substitute: 30px on Windows, 102 on macOS, 127 on Linux. Windows keeps the measured 64/32px bound; elsewhere it is a tenth of the block. The fixed point that follows (identical metadata, byte-identical third apply) is platform-independent and unchanged.
- `ui_font_picker_applies_a_pick_the_control_already_shows` sets the picker to a family the database lacks. Doing that makes CoreText (and fontconfig on its first miss) fill in font-family aliases lazily, and that repopulation re-emits the combo's current row, whose handler writes the displayed family back over the missing one - so the control never held the state under test. Aliases fill in once, so the test sets it a second time. `ui_text_font_picker_preview_shows_supported_scripts` has the same shape of fix: it takes the first Japanese family the PICKER lists rather than the font database's first, because `QFontComboBox`'s model drops families the database still reports.

## macOS synthesized font stretch (Photoshop HorizontalScale)

Qt 6.8's CoreText engine applies `QFont::setStretch` to advances TWICE: the stretch is baked into the CTFont matrix and `loadAdvancesForGlyphs` multiplies the already-scaled advances by it again (qfontengine_coretext.mm), so stretch 90 rendered at 81% and the SNES box blurb lost 10% of its line length against Photoshop's raster on macOS. DirectWrite/GDI scale advances once; FreeType derives the face's x char-size from the stretch, also once. `set_stretch_for_advance_ratio` (main_window.cpp) therefore decides between the historical `round(ratio x 100)` encoding and the sqrt-compensated one by MEASURING the font at hand (which candidate's 'H' advance lands the requested ratio). Per font, not per platform or per process: a font with a native width axis answers `setStretch` through real width matching (`.AppleSystemUIFont` measured x0.39 at stretch 50, neither linear nor squared, and a one-per-process probe cached off it and broke the suite's ordered runs), and a missing family's fallback engine ignores stretch entirely (both error measurements tie, keeping the historical value). On linear engines the naive candidate measures ~exact and always wins, so Windows/Linux pinned pixels stay bit for bit; a Qt release that fixes CoreText flips macOS back by itself, and the SNES box probes pin the rendered result. Accepted residue: glyph OUTLINES on macOS render sqrt(ratio) wide (the matrix half of the double application); advances, line breaks, and ink extents are what Photoshop parity needs.

## macOS non-modal dialog anchoring

On macOS, `run_non_modal_dialog` anchors the dialog as a native child window of its parent widget's window (`keep_dialog_above_parent_window`, dialog_utils_mac.mm): macOS has no Win32 owned-window z-order, so clicking the edit-locked main window buried the dialog behind it and the app looked frozen (0.13 mac bug). The anchor attaches on Show (deferred one event-loop turn) and MUST detach on Hide/Close because AppKit re-orders attached children with their parent even when hidden. Child windows follow parent moves; that is accepted mac-native behavior. Any new non-modal dialog path that bypasses `run_non_modal_dialog` needs the same call.
