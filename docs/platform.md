# Platform notes (macOS/Linux ports)

Deep reference for cross-platform work. Read this before hunting a platform-specific regression, adding a platform-guarded site, or changing packaging/build configuration.

## Cross-platform implementation rules

Linux offscreen app and MCP startup disables the process's desktop D-Bus session
before constructing QApplication. Qt loads the Flatpak portal theme even for
offscreen and synchronously queries appearance; a missing portal otherwise delays
startup and client EOF. Interactive desktop runs retain the normal bus and portals.

Windows is the lead platform and must not regress. Every code change still completes the Windows release handoff in `AGENTS.md`. Changes to platform guards, CMake files/presets, or packaging additionally run the affected macOS and/or Linux remote build best-effort and report its result.

- Prefer a small local `#ifdef Q_OS_WIN`, `Q_OS_MACOS`, or `Q_OS_LINUX` (`_WIN32` in Qt-free code) with a portable fallback. Split into `foo_win.cpp`, `foo_mac.mm`, and `foo_linux.cpp` behind `WIN32`, `APPLE`, and `UNIX AND NOT APPLE` only when the site needs Objective-C++/system frameworks or outgrows about one screenful. Per-OS files live beside their feature, not in a platform directory.
- Window-frame code stays in `main_window_chrome.cpp`. `MainWindow::use_custom_window_chrome()` is the only gate: true on Windows for the frameless window, custom title-bar controls, and Qt edge resizing; false on macOS/Linux for native frames. macOS uses the native global menu bar, so never call `setNativeMenuBar(false)` outside gated `configure_window_chrome()` code.
- Tests obtain fonts through `tests/test_fonts.hpp`. Its Windows candidates preserve historical baselines. Triage a macOS/Linux failure by fixing a real bug first, then a platform-specific skip with a reason, then a platform-specific baseline. Never loosen a tolerance globally to make one platform pass.
- File formats remain byte-identical across operating systems. Use explicit endian and fixed-width primitives; never serialize `size_t`, `long`, `wchar_t`, or raw structs.
- Read environment variables through `core/environment.hpp`. The helper returns an owned string so callers never retain process-environment storage, and its Windows implementation avoids the deprecated `getenv` diagnostic.
- Never derive a font size by arithmetic on `pointSizeF()`. A QFont carries its size in either points or pixels and the unused accessor returns -1; macOS resolves inherited widget fonts by pixel size, so `f.setPointSizeF(f.pointSizeF() * 0.85)` asks for -0.85 there. Qt refuses it (`QFont::setPointSizeF: Point size <= 0`) and leaves the font unchanged, so the text renders at full size on macOS only. Clamping with `std::max(7.0, ...)` only hides the warning. Use `scale_font_size` / `scaled_font` / `offset_font` from `dialog_utils.hpp`; they branch on the unit the font actually carries.

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
`mac-dev` or `linux-dev` verification. The snapshot push lives in
`scripts\remote\remote-snapshot.ps1`. Do not add `-Werror` or `/WX`; a new warning is
fixed in source or isolated at the exact vendored source and diagnostic.

GCC-only diagnostics MSVC never reports: `-Wmissing-field-initializers` fires when a
positional aggregate initializer stops before a member with no default member initializer
(a new trailing member breaks every older `T{a, b, c}` site; use designated initializers),
and `-Wunused-function` fires for a dead helper in an anonymous namespace (delete it).

### The flatpak build is a second Linux compiler

A warning reported by the flatpak release build usually cannot be reproduced or
verified by `remote-build.ps1 -Target linux`. The two use different compilers and
different flags: `linux-release` on the linux build host is the system `/usr/bin/c++` (GCC 13.3.0 on
Ubuntu 24.04) with only `-O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic`, while
`packaging/linux/make-flatpak.sh` builds inside `org.kde.Sdk//6.11` with GCC 15.2.0 and
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

    flatpak run --filesystem=home --command=g++ org.kde.Sdk//6.11 <INCLUDES> <FLAGS> \
      -c ~/patchy/src/src/formats/af_document_io.cpp -o /dev/null

Compiling a copy of the pre-fix file the same way confirms a fix actually removed the
warning instead of merely being compiled by something that never reported it.

## Remote build machinery

The reachable build machines are developer-specific and never named in the repository. Their ssh targets live in `scripts/remote/hosts.local.json` (gitignored; copy `scripts/remote/hosts.example.json`), which every remote script reads through `scripts/remote/remote-hosts.ps1`. Hardware notes and per-machine quirks go in `agents_local.md`. Both files live in the main checkout (worktree runs fall back to it). Below, `<mac-build-host>`, `<linux-build-host>` and `<windows-build-host>` are those ssh targets.

macOS (arm64, preset `mac-release`, Qt at `.deps/Qt/6.8.3/macos`) and Linux (preset `linux-release`, Qt at `.deps/Qt/6.8.3/gcc_64`) build remotely via `scripts\remote\remote-build.ps1 -Target mac|linux`, which snapshots the working tree (uncommitted changes included; no commits, branches or index changes) to a bare repo (`~/patchy.git`) on the build host, builds there, and runs both suites with output streamed back. One-time machine provisioning is `scripts/remote/setup-mac.sh` / `setup-linux.sh` (idempotent: venv tools + Qt via aqtinstall + apt deps).

`remote-build.ps1` checks out over the one shared `~/patchy/src`, so first check nobody else is using the box: `ssh <linux-build-host> "pgrep -af 'patchy_|ninja|cc1plus'"`. When it is busy, do not wait on or kill that run: push the snapshot to a private ref (`refs/snapshots/<topic>`), clone `~/patchy.git` into `~/patchy-<topic>/src`, symlink `.deps` to `~/patchy/src/.deps`, configure and build the preset there with the environment lines from `build-and-test.sh`, and delete the clone and the ref afterwards.

### Windows offload build (on request only)

`remote-build.ps1 -Target windows` builds the real MSVC `release` preset on a second Windows machine. Use it only when Seth explicitly asks to offload a Windows build. The local `build\release` stays the release gate and the only packaging source.

- **Layout.** The bare repo is `~\patchy.git` (same push URL shape as mac/linux) and the work tree is the host config's `workTree`. Qt is the dev box's `.deps\Qt\6.8.3\msvc2022_64`, copied to the same relative path, so the unmodified `release` preset resolves it.
- **Toolchain.** VS 2026 Build Tools (MSVC x64, Windows SDK 10.0.26100, VS-bundled CMake and Ninja), Git for Windows, and Python 3.13 (the release configure needs Python 3.8+; the WindowsApps `python.exe` is only a Store alias). `scripts\vs-env.bat` finds Build Tools through vswhere when the dev box's Community path is absent. Bootstrapper: `https://aka.ms/vs/18/stable/vs_buildtools.exe` (the `release` URL redirects to Bing).
- **Provisioning.** `scripts/remote/setup-windows.ps1 -Root <dir>` is idempotent and prints the one-time Qt copy command when Qt is missing. Its header has the scp and ssh lines. The ssh account must be an administrator.
- **Remote half.** `scripts/remote/build-and-test.ps1` configures, builds with `-j 8` through `run-throttled.bat` (below-normal priority, so the machine stays usable), prints whether `patchy.exe` exists, and runs both suites. `-TestFilter` filters the UI suite as on mac/linux; `-CoreTestFilter` (windows only) filters the core suite. Per-change runs follow the same scoped-filter rule as the dev box.
- **Shell quirks.** A Windows OpenSSH server's default shell is Windows PowerShell 5.1: no `&&`, and ad-hoc remote scripts are best sent as a file because local PowerShell expands `$_` and mangles embedded double quotes. `Get-Process a,b` exits 1 when any name is absent, so do not chain it with `&&`.
- **Smart App Control must be off.** The build runs fresh unsigned executables (`patchy_translation_checks.exe`, then the suites); with it on, Code Integrity blocks them at random ("blocked by your organization's Device Guard policy", events 3033/3077). It has no per-folder exclusions and turning it off is irreversible without reinstalling Windows, so it is the machine owner's decision. Check `(Get-MpComputerStatus).SmartAppControlState`.
- **Busy check.** One shared work tree, like the linux host: first run `ssh <windows-build-host> "Get-Process cl,link,ninja,cmake,patchy_core_tests,patchy_ui_visual_tests -ErrorAction SilentlyContinue"`, and never kill another session's build there.

## AddressSanitizer runs (order-dependent heap bugs)

The `linux-asan` preset (RelWithDebInfo + `-fsanitize=address`, own `build/linux-asan` dir) is the tool for crashes that only reproduce in the full ordered suite (teardown and store use-after-frees). Sync the tree with `remote-build.ps1 -Target linux -SkipTests`, then build and run the instrumented suites on the box:

    ssh <linux-build-host> "ASAN_OPTIONS='quarantine_size_mb=8192:malloc_context_size=25:detect_leaks=0' \
      bash ~/patchy/src/scripts/remote/build-and-test.sh linux-asan"

The large quarantine keeps long-ago frees poisoned for the whole run, so the host needs plenty of RAM; `detect_leaks=0` keeps exits quiet. The UI suite's POSIX SIGSEGV/SIGBUS reporter steps aside under ASAN (`tests/ui/main.cpp`). A fresh build dir also surfaces stale `test-artifacts` expectations that long-lived dirs hide ([testing.md](testing.md)).

## Platform-specific site inventory (keep current)

- Windows Explorer branding: `src/app/patchy.rc.in` embeds the display name and
  project version in every app build; the installer registers the same
  `Patchy Image Editor` name. See [Windows packaging](../packaging/windows/README.md#open-with).
- `main_window_chrome.cpp` + the `use_custom_window_chrome()` call sites in `main_window.cpp` (frameless flag, chrome controls).
- `psd_document_io.cpp` DirectWrite font resolution + wide-string helpers (portable heuristic fallback).
- `layer_list_widget.cpp` drag-wheel low-level mouse hook (degrades gracefully).
- `canvas_alt_space_filter.cpp` (Windows only, inert elsewhere): Qt's Windows key mapper never delivers Space while Alt is held; it opens the window's system menu itself (a modal `TrackPopupMenu` loop that ate the mouse release and stranded Alt-from-center drags repositioned with Space; GitHub issue 78). The app-level native filter, installed once from the `CanvasWidget` constructor, runs ahead of the key mapper: while the focused canvas has `pointer_gesture_active()` it consumes the `VK_SPACE` `WM_SYSKEYDOWN`/`WM_SYSCHAR`/key-up and sends the canvas plain Space key events; idle, Alt+Space still opens the menu. `ui_marquee_alt_space_mid_drag_repositions_instead_of_system_menu` feeds it crafted `MSG`s (offscreen has no Win32 loop).
- `dialog_utils.cpp` `use_qt_file_dialog_controls` (Qt dialog widgets only under offscreen; native/portal dialogs otherwise, on every OS).
- The Print dialog's "Print Using System Dialog..." is always `QPrintDialog`, with no offscreen switch, so the UI test checks the button and never clicks it. On Windows 11 22H2+ that is the "unified" `PrintDlgEx` dialog, which shows "This app doesn't support print preview" to every Win32 caller (only WinRT `PrintManager` apps get a preview there). Do not switch to the classic `PrintDlg` + `PD_ENABLEPRINTHOOK` dialog again (tried August 2026 for a `0x80070002` error that was a broken Microsoft Print to PDF install; it only hides the driver's options behind Properties...).
- `print_dialog.cpp` `ensure_printer_driver_usable` / `windows_printer_device_is_usable` (Windows only): when `CreateDC` fails (a Microsoft Print to PDF with a broken port registration, `ERROR_PATH_NOT_FOUND`), `QWin32PrintEnginePrivate::initialize` nulls the DEVMODE and `QPageSetupDialog::exec` crashes on it, while the spooler still hands out a DEVMODE. The guard therefore tests the device context, not the DEVMODE, and Print, Page Setup, and the system dialog report "Printer unavailable" instead.
- Windows print-color complaints (right on screen, shifted on paper) are not Patchy's: the spooled PCL XL stream is byte-exact (verified August 2026 by giving a `NativeFormat` `QPrinter` a non-`.pdf` output name, which `StartDoc` writes to), and its PJL header shows RGB-to-CMYK happens in printer firmware. Converting the composite before `drawImage` changes nothing (`QWin32PrintEngine` re-tiles to opaque RGB32); fix it on the printer.
- `dialog_utils_mac.mm` `keep_dialog_above_parent_window` (macOS child-window anchor for non-modal dialogs - see [ui-conventions.md](ui-conventions.md); no-op elsewhere; first Objective-C++ TU, `enable_language(OBJCXX)` is APPLE-gated in CMakeLists).
- `dialog_utils_mac.mm` `move_pointer_to_global_position` (October 2026): Qt's cocoa `QCursor::setPos` posts a synthetic CGEvent, so macOS asks the user to let Patchy control the computer (Accessibility). The macOS half calls `CGWarpMouseCursorPosition` instead, which needs no permission. Never call `QCursor::setPos` directly in `src/ui`; the one caller is the Alt+Right-drag brush adjust snapping the pointer back to its anchor.
- `dialog_utils.cpp` `screen_color_at_global_position` (the canvas eyedropper outside the document and the color picker's screen eyedropper): `QScreen::grabWindow` raises the macOS Screen Recording permission prompt, so on macOS it first renders the Patchy window under the point (`own_window_color_at_global_position`, a `QWidget::grab`, no permission). Only a pick outside Patchy's windows or on a native title bar reads the screen there; other platforms keep the plain screen read. `ui_own_window_color_sample_reads_widget_without_screen_grab` pins the helper.
- Scanner import uses Windows WIA in `scanner_import_win.cpp` and macOS ImageKit/ImageCaptureCore in `scanner_import_mac.mm`; the macOS browser exposes local and network scanners but not cameras, and acquisition remains single-image on both platforms. The AppKit sheet is callback-based so the File menu action returns to the native run loop; never wait for it with a nested `QEventLoop` (its controls stay visible but take no mouse input).
- The app stylesheet's `QCheckBox { border: none }` (macOS Aqua layout-item margin suppression - see [ui-conventions.md](ui-conventions.md)) and its APPLE-gated `QGroupBox` block + `brush_dynamics_popup.cpp` `compact_group_grid` (QMacStyle's Aqua group-box chrome and layout spacings blow dense panels past the screen; Windows keeps native metrics).
- `dialog_utils.cpp` `suppress_native_tab_bar_base` (macOS document-mode tab bars paint a light native base across their width even though the `::tab` rules still apply, so the document tabs and Preferences tabs drop the base; no-op elsewhere).
- `main.cpp` `InteractionHintsStyle::styleHint` macOS block (pins SH_FormLayoutFieldGrowthPolicy / LabelAlignment to the Windows behavior because QMacStyle otherwise keeps form fields at size-hint and right-aligns labels, shrinking Name/Folder-style edits to slivers) and the APPLE-gated QScrollBar block in `photoshop_style()` (Windows-classic dithered track via scroll-dither.svg, flat bordered handle, no arrow buttons since fixed-size QSS line buttons make the groove degenerate on short scrollbars in collapsed docks; QMacStyle's flat overlay bars hide the handle on the dark theme).
- `main_window_files.cpp` `reveal_path_in_file_explorer`: the Windows branch must pass `/select,` and the file path as SEPARATE QProcess arguments. QProcess quotes any space-containing argument whole, and Explorer reads a quoted `"/select,path"` blob as unparseable, silently opening the default folder.
- `network_mounts.cpp` `read_system_mounts` (macOS `getmntinfo(MNT_NOWAIT)`, Linux `/proc/self/mounts`, empty on Windows where `GetDriveTypeW` answers): the recent-history existence check skips paths on non-local mounts and `autofs` triggers, because a stat on a stale share blocks for the whole network timeout and `main()` waits for that worker at quit. Reads cached kernel data only; never replace it with `statfs` on the path, which contacts the server. `ui_network_mount_classification_uses_deepest_mount_point` pins the matcher on a synthetic table.
- `main_window_files.cpp` `rebuild_recent_files_menu`: the recent-files filter row (a QWidgetAction-hosted line edit) is skipped when `menuBar()->isNativeMenuBar()` is true, because native menu bars (the macOS global bar, Linux appmenu desktops) cannot host widget actions; those platforms keep the plain paged menu. The check is false under `QT_QPA_PLATFORM=offscreen`, so the UI tests exercise the filter row on every platform.
- Optional Qt add-ons get their OWN `find_package(Qt6 QUIET COMPONENTS <X>)` and a `if(TARGET Qt6::X)` source swap, never a slot in `PATCHY_QT_COMPONENTS`: that lookup is QUIET, so one missing component silently skips the entire app target. Qt PDF is the current example (`pdf_import.cpp` vs `pdf_import_stub.cpp`, `PATCHY_HAVE_QT_PDF`); QtPrintSupport uses the same swap shape keyed on EMSCRIPTEN (Qt publishes no wasm build of it). Both stubs must keep every entry point real enough to report the feature as unavailable.
- `update_checker.cpp` platform id (windows/macos/linux manifest keys).
- `single_instance.cpp` (Windows only): a relaunch grants `AllowSetForegroundWindow` to the pipe's server process (`GetNamedPipeServerProcessId`) before forwarding; the foreground lock otherwise refuses the running instance's activation and its window only flashes on the taskbar. The receiver comes forward on arrival (`bring_to_front_for_second_instance`, modal dialog first): the grant expires before a deferred file open runs.
- `main.cpp` Windows app-font candidates + macOS `Contents/Resources` probes (with `localization.cpp`'s translations probe).
- File paths: every `QString` -> `std::filesystem::path` conversion goes through `to_filesystem_path` (and the reverse through `to_qstring`) in `ui/qt_paths.hpp`, which uses `toStdU16String()` (UTF-16 -> native on every platform; `toStdString()` is UTF-8 and MSVC decodes a narrow `std::string` with the ANSI code page, mangling non-ASCII names; `toStdWString` is UTF-32 on POSIX). Path pieces become UTF-8 text through `path_to_utf8` in `support/path_utils.hpp`, never `path::string()` (ANSI on MSVC, `?` for every unrepresentable character). Patchy deliberately ships no UTF-8 `activeCodePage` manifest (it would cover only `patchy.exe`, flip every ANSI API process-wide and hide the wrong idiom). Pinned by the `unicode_path_*` core group and the `ui_unicode_*` UI group (names in `tests/unicode_path_names.hpp`); the UI tests compare names NFC-normalized because macOS stores NFD, the core tests use `std::filesystem::equivalent`.
- Tests: `test_harness.hpp`, the paired crash reporters in `tests/ui/main.cpp`, `test_fonts.hpp`.

## Color scheme and native chrome

`ThemeManager` mirrors the resolved scheme onto Qt with `QStyleHints::setColorScheme` (`unsetColorScheme` for "follow system"). On Windows that re-tints the chrome Patchy does not style (native dialog title bars, dock and list scroll bars, tooltips, `QMessageBox`, the color picker), so there is no `DWMWA_USE_IMMERSIVE_DARK_MODE` call and no registry read. Under `QT_QPA_PLATFORM=offscreen` the call is a no-op and `colorScheme()` reports `Unknown`, which keeps the UI suite deterministic regardless of the host's setting.

The frameless main window has no native title bar to tint; it draws its own 1px edge from the `window_border` role, overridden by the Light palette so the window keeps a silhouette on a light desktop. `DWMWA_BORDER_COLOR` in `main_window_chrome.cpp` stays unconditionally `COLOR_NONE`.

## Styled QCheckBox and QMacStyle margins

A stylesheet-styled QCheckBox needs a NON-native border in some matching rule. The app stylesheet's global `QCheckBox { border: none; }` covers this; do not remove it. Qt only suppresses QMacStyle's Aqua layout-item margins (checkboxes: +2,+3,-9,-4) for styled widgets whose rule has a non-native border (qstylesheetstyle.cpp, SE_*LayoutItem). With the margins active, box layouts overlap the neighboring label ~9px into the checkbox: right for the inset native glyph, but on the flat 12px stylesheet indicator the label lands ON the box (the 0.13-mac Layer Style bug). Only reproducible in the real app: the test harness never loads QMacStyle.

## Per-platform test skips (keep this list current)

On macOS/Linux: `ui_bundled_legacy_plugin_action_applies_filter`, `ui_transparency_checkerboard_and_copy_paste_preserve_alpha`, `ui_legacy_plugin_run_respects_selection_and_undoes`, `ui_legacy_plugin_mehdi_filters_run_if_available`, `ui_legacy_plugin_mehdi_dialog_capture_if_available`, `ui_legacy_plugin_menu_opens_folder_and_rescans` and `ui_about_dialog_has_plugins_folder_row` (the legacy 8BF host and its folder UI are Windows-only, docs/plugins.md; `ui_legacy_plugin_ui_absent_off_windows` runs instead) plus the core `legacy_host_self_test_filters_fixtures_in_both_bitnesses` (not registered off Windows), `ui_frameless_window_edges_resize` (gated on `use_custom_window_chrome()`), and the two `ui_imported_psd_box_text_line_clip_*` tests (pin Windows Arial line metrics; CoreText/fontconfig differ by a few px). `ui_main_window_renders_color_controls` asserts frameless/badge/window-buttons **presence on Windows and absence elsewhere**. Local-fixture tests `[SKIP]` on a remote until the corpus is copied there ([testing.md](testing.md)).

Text tests additionally gate on the fixture's own face. All three guards live in `tests/ui/ui_test_support.cpp`, for two reasons: a substituted face turns a pinned geometry tolerance into a measurement of the substitute, and a text session without the face raises the Missing Font prompt, which nothing answers under offscreen (the suite HANGS in the nested dialog loop).

- `skip_without_arial_for_psd_text_preview()` gates seven imported-PSD raster-preview text tests, the three `ui_af_*` text tests, and `ui_warp_text_render_matches_photoshop_if_available`. macOS ships Arial (`/System/Library/Fonts/Supplemental`) and runs all eleven; Linux ships Liberation, not Arial, and skips them.
- `skip_without_font_face(family, role)` gates on a face being reachable at all, matching MainWindow's family-or-`"family style"` split (Qt files Arial Black under family Arial, style Black). Candara (`restaurant-menu-inside.psd`) and Bookman Old Style (`dungeon-scroll-game-screen.psd`) are Windows-only; Georgia is Windows and macOS.
- `skip_without_psd_text_face(layer, family)` covers the other way a machine lands off the Windows baseline: the family the imported layer actually resolved to. PSD PostScript names go through DirectWrite on Windows and the Qt font database elsewhere (`install_font_database_psd_font_resolver` in `src/ui/psd_font_resolver.hpp`, installed by the app and the UI test main; the suffix-stripping heuristic in `psd_text_read.cpp` is the last resort; wasm stays heuristic-only so bundled alias families never reach imported metadata). The database resolver handles both shapes a face takes off Windows, whole family ("Arial Black") and family "Arial" + style "Black", mirroring the DirectWrite output rules (flag-expressible faces stay family + flags, mid weights keep the real face without the bold flag, Black/Heavy keeps the full name plus bold). The guard fires only when the face was absent at read time: the `snes-box-a3.psd` probes run on Windows and macOS and skip on Linux (no Arial Black). Tests must register a fixture's face BEFORE reading the PSD.

## Remote suites with the fixture corpus present

`local-test-fixtures/` is copied to the mac and linux build hosts, so both remotes run the fixture-gated text tests and every suite must pass there; a `[SKIP]` line reports each thing a machine cannot cover (one per missing `local-test-fixtures` PSD). `composite_corpus_flatten_digests_are_stable` runs against the tracked baselines in `test-fixtures/psd` everywhere: the CPU compositor is byte-identical across Windows, macOS arm64/clang, Linux x86-64/GCC and wasm. Never re-pin those baselines per machine.

Most corpus differences are the face gates listed above; three tests needed something else:

- `ui_warp_text_render_matches_photoshop_if_available` (macOS only; Linux has no Arial) compares Patchy's warp against Photoshop PNGs of Windows Arial. CoreText costs ~0.06 of IoU and ~2px of ink extent on the point-text cases without moving the warp geometry, so off Windows the IoU floors scale by 0.90 and the bounds tolerance gains 2px. Every case reports its numbers before failing.
- `ui_duke_psd_text_runs_survive_reedit` reflows `Duke nukem mobile.psd`'s body between the first and second apply. Its face (FuturaLT-ExtraBold) is installed nowhere, so each platform's substitute reflows: 30px on Windows, 102 on macOS, 127 on Linux. Windows keeps the measured 64/32px bound; elsewhere it is a tenth of the block. The fixed point that follows (identical metadata, byte-identical third apply) is platform-independent.
- `ui_font_picker_applies_a_pick_the_control_already_shows` sets the picker to a family the database lacks. CoreText (and fontconfig on its first miss) then fills in font-family aliases lazily, and that repopulation re-emits the combo's current row, whose handler writes the displayed family back over the missing one. Aliases fill in once, so the test sets it a second time. `ui_text_font_picker_preview_shows_supported_scripts` likewise takes the first Japanese family the PICKER lists, not the database's first: `QFontComboBox`'s model drops families the database still reports.

## macOS menubar submenus and Qt's menu-role heuristic

Qt's Cocoa plugin merges an item directly under a top-level menu into the app menu when its title starts with the translated "About", "Config", "Preference", "Options", "Setting", "Setup", "Quit" or "Exit", re-checked on every sync, so a language switch flips items ("Ajustes", "Réglages"). A merged SUBMENU crashes Qt 6.8.3 on the next window activation (`setSubmenu:` on its freed old `NSMenuItem`; GitHub issue 29). Every menubar submenu action gets `QAction::NoRole` (`exclude_submenus_from_native_menu_roles` and the dynamic submenu builders; `ui_menubar_submenus_have_no_native_menu_role` guards it, `ui_language_switch_survives_window_reactivation` under `PATCHY_UI_TEST_PLATFORM=cocoa NSZombieEnabled=YES` reproduces the crash).

## macOS synthesized font stretch (Photoshop HorizontalScale)

Qt 6.8's CoreText engine applies `QFont::setStretch` to advances TWICE: the stretch is baked into the CTFont matrix and `loadAdvancesForGlyphs` multiplies the already-scaled advances by it again (qfontengine_coretext.mm), so stretch 90 rendered at 81% on macOS. DirectWrite/GDI and FreeType apply it once. `set_stretch_for_advance_ratio` (main_window.cpp) therefore decides between the historical `round(ratio x 100)` encoding and the sqrt-compensated one by MEASURING the font at hand (which candidate's 'H' advance lands the requested ratio). Per font, not per platform or per process: a font with a native width axis answers `setStretch` through real width matching (`.AppleSystemUIFont` measured x0.39 at stretch 50, neither linear nor squared; a one-per-process probe cached off it), and a missing family's fallback engine ignores stretch (both measurements tie, keeping the historical value). On linear engines the naive candidate wins, so Windows/Linux pinned pixels are unchanged; a Qt release fixing CoreText flips macOS back by itself, and the SNES box probes pin the result. Accepted residue: macOS glyph OUTLINES render sqrt(ratio) wide (the matrix half); advances, line breaks and ink extents are what Photoshop parity needs.

## macOS non-modal dialog anchoring

On macOS, `run_non_modal_dialog` anchors the dialog as a native child window of its parent widget's window (`keep_dialog_above_parent_window`, dialog_utils_mac.mm): macOS has no Win32 owned-window z-order, so clicking the edit-locked main window buried the dialog behind it (0.13 mac bug). The anchor attaches on Show (deferred one event-loop turn) and MUST detach on Hide/Close because AppKit re-orders attached children with their parent even when hidden. Any new non-modal dialog path that bypasses `run_non_modal_dialog` needs the same call.
