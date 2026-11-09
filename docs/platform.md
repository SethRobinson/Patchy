# Platform notes (macOS/Linux ports)

Deep reference for cross-platform work. Read it before hunting a platform-specific regression, adding a platform-guarded site, or changing packaging or build configuration.

Related documents:

- [platform-sites.md](platform-sites.md): the platform-specific site inventory, every platform-guarded site with its reason and rules; list a new guard there.
- [platform-tests.md](platform-tests.md): per-platform test skips, the font-face gates of the text tests, and the macOS and Linux suites run with the fixture corpus.

## Cross-platform implementation rules

Linux offscreen app and MCP startup disables the process's desktop D-Bus session
before constructing QApplication: Qt's Flatpak portal theme queries appearance
synchronously even offscreen, and a missing portal delays startup and client EOF.
Interactive desktop runs keep the bus and portals.

Windows is the lead platform and must not regress. Every code change completes the Windows opt-dev handoff in `AGENTS.md`. Changes to platform guards, CMake files/presets, or packaging additionally run the affected macOS and/or Linux remote build best-effort and report its result.

- Prefer a small local `#ifdef Q_OS_WIN`, `Q_OS_MACOS`, or `Q_OS_LINUX` (`_WIN32` in Qt-free code) with a portable fallback. Split into `foo_win.cpp`, `foo_mac.mm`, and `foo_linux.cpp` behind `WIN32`, `APPLE`, and `UNIX AND NOT APPLE` only when the site needs Objective-C++/system frameworks or outgrows about one screenful. Per-OS files live beside their feature, not in a platform directory.
- Window-frame code stays in `main_window_chrome.cpp`. `MainWindow::use_custom_window_chrome()` is the only gate: true on Windows and Linux (frameless window, the title-bar controls in the menu-bar row, edge resizing), false on macOS, which keeps the native frame and global menu bar, so never call `setNativeMenuBar(false)` outside gated `configure_window_chrome()` code. On Linux the preference key `window/systemTitleBar` (a compatibility contract, read once at startup) turns the gate off. Rules: [wayland.md](wayland.md).
- The Windows title bar is native behind a custom look. `MainWindow::nativeEvent` answers `WM_NCHITTEST` with `HTCAPTION` for the blank menu-bar area (`title_bar_drag_area_contains`: not a menu title, not the window controls, not a resize border), so the OS owns drag, snap, the system menu, double-click maximize/restore, and restore-on-drag; Qt never sees those presses. The menu-bar press/move/double-click handling in `MainWindow::eventFilter` is the path on Linux (a drag past the threshold becomes `startSystemMove`) and offscreen, with the same contract: a plain click never changes the window state, a drag waits for `QApplication::startDragDistance()`, a double-click toggles. Never move or restore a window from the press itself.
- Tests obtain fonts through `tests/test_fonts.hpp`. Its Windows candidates preserve historical baselines. Triage a macOS/Linux failure by fixing a real bug first, then a platform-specific skip with a reason, then a platform-specific baseline. Never loosen a tolerance globally for one platform.
- File formats remain byte-identical across operating systems. Use explicit endian and fixed-width primitives; never serialize `size_t`, `long`, `wchar_t`, or raw structs.
- File paths (rule in AGENTS.md): `to_filesystem_path`/`to_qstring` (`ui/qt_paths.hpp`) use `toStdU16String()`, UTF-16 to native on every platform (`toStdWString` is UTF-32 on POSIX); `path_to_utf8` replaces `path::string()`, which is ANSI on MSVC with `?` for unrepresentable characters. Patchy deliberately ships no UTF-8 `activeCodePage` manifest (it would cover only `patchy.exe`, flip every ANSI API process-wide and hide the wrong idiom). Pinned by the `unicode_path_*` core group and the `ui_unicode_*` UI group (names in `tests/unicode_path_names.hpp`); the UI tests compare names NFC-normalized because macOS stores NFD, the core tests use `std::filesystem::equivalent`.
- Read environment variables through `core/environment.hpp`. The helper returns an owned string so callers never retain process-environment storage, and its Windows implementation avoids the deprecated `getenv` diagnostic.
- Never derive a font size by arithmetic on `pointSizeF()`. A QFont carries points or pixels and the unused accessor returns -1; macOS resolves inherited widget fonts by pixel size, so `f.setPointSizeF(f.pointSizeF() * 0.85)` asks for -0.85, which Qt refuses (`Point size <= 0`), leaving full-size text on macOS only. Clamping only hides the warning. Use `scale_font_size` / `scaled_font` / `offset_font` from `dialog_utils.hpp`; they branch on the unit the font actually carries.

## Floating-point determinism

Pixel output must be bit-identical on MSVC x64, macOS arm64 clang, Linux x86-64 GCC and wasm, so pins and digests hold everywhere. The trap is fused multiply-add: arm64 has it in the base ISA and clang contracts `a * b + c` into one FMA by default (`-ffp-contract=on`), rounding once where MSVC (`/fp:precise` forbids contraction) and x86-64 GCC (no FMA without `-mfma`) round twice. One ulp before an 8-bit quantize is enough to flip a level. GCC and clang therefore build every float-heavy pixel target with `-ffp-contract=off`, set in `CMakeLists.txt`: `patchy_filters` (all filter, Smart Filter and Look kernels), `src/core/perspective_planes.cpp` ([perspective-planes.md](perspective-planes.md)) and the vendored DDS encoders (stb_dxt, bc7enc; [dds.md](dds.md)).

- Evidence: with contraction on, 9 of the 20 built-in Look renders (`builtin_looks_render_pins_8_bit`) hashed differently on macOS arm64 (Punchy Color, High Pass, Gaussian Blur under Divide, Brightness/Contrast, Tilt-Shift Blur); with it off all 20 match the MSVC digests, so libm differences play no part. MSVC output is unchanged by the flag.
- A new library or source with float pixel math gets the same flag, or uses integer math. Never pin per-platform digests or loosen a tolerance on one platform to hide a contraction difference ("Cross-platform implementation rules" above).
- One random draw per statement; call order within an expression differs by compiler.
- Never call `std::fma` or add `-ffast-math`, `-mfma` or `/fp:fast`/`/fp:contract`: each changes rounding on some toolchains only.

## Warning verification

Patchy keeps warnings non-fatal, but supported clean builds must produce no compiler,
linker, or `lrelease` warnings. Build the default target so the application and every
test binary compile.

| Platform | Debug preset | Release preset |
|---|---|---|
| Windows MSVC | `debug` | `release` |
| Windows MSVC ARM64 | none (`windows-arm64-opt-dev`) | `windows-arm64-release` |
| macOS Clang arm64 | `mac-dev` | `mac-release` |
| Linux GCC x64 | `linux-dev` | `linux-release` |

For warning verification, clean each build directory, configure and build the preset,
and search the combined stdout/stderr log for `warning` and `error`. A clean Release
build on each platform also runs both full suites. Run
`patchy_curves_clipping_preview_tests` and `patchy_perf_tests` when validating a
cross-platform cleanup or changing their dependencies.

`scripts\remote\remote-build.ps1 -Target mac|linux` exercises the Release preset; after
it snapshots the tree (`scripts\remote\remote-snapshot.ps1`), use that remote checkout
for clean `mac-dev` or `linux-dev` verification. Do not add `-Werror` or `/WX`; a new
warning is fixed in source or isolated at the exact vendored source and diagnostic.

GCC-only diagnostics MSVC never reports: `-Wmissing-field-initializers` fires when a
positional aggregate initializer stops before a member with no default member initializer
(a new trailing member breaks every older `T{a, b, c}` site; use designated initializers);
`-Wunused-function` fires for a dead helper in an anonymous namespace (delete it).

### The flatpak build is a second Linux compiler

A warning reported by the flatpak release build usually cannot be reproduced or
verified by the `linux-release` preset. The two use different compilers and flags: `linux-release` on the linux build host is the system `/usr/bin/c++` (GCC 13.3.0 on
Ubuntu 24.04) with only `-O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic`, while
`packaging/linux/make-flatpak.sh` builds inside `org.kde.Sdk//6.11` with GCC 15.2.0 and
flatpak-builder's hardening flags (`-Wp,-D_FORTIFY_SOURCE=3 -Wp,-D_GLIBCXX_ASSERTIONS
-fstack-protector-strong -fstack-clash-protection -fcf-protection -fno-omit-frame-pointer`,
then `-O3 -DNDEBUG` on top). The newer libstdc++ headers and `_GLIBCXX_ASSERTIONS`
change inlining enough to produce optimizer false positives (`-Wfree-nonheap-object`,
`-Warray-bounds=`) the plain preset never emits.

Checking one translation unit is far cheaper than a full flatpak run. Take the object's
`FLAGS` and `INCLUDES` from a previous flatpak build tree
(`packaging/linux/.flatpak-builder/build/patchy-*/build.ninja`, which survives
`--force-clean`) and compile it with the SDK compiler:

    flatpak run --filesystem=home --command=g++ org.kde.Sdk//6.11 <INCLUDES> <FLAGS> \
      -c ~/patchy/src/src/formats/af_document_io.cpp -o /dev/null

Compiling a copy of the pre-fix file the same way confirms a fix removed the warning
rather than being compiled by something that never reported it.

## Remote build machinery

Remote builds (ssh targets, the per-host lock and busy probe, the Windows offload hosts) are in [building.md](building.md#remote-build-machinery); `-BuildTarget`, `-Preset` and one-target iteration are in [building-incremental.md](building-incremental.md#remote-iteration-one-target-opt-dev).

## AddressSanitizer runs (order-dependent heap bugs)

The `linux-asan` preset (RelWithDebInfo + `-fsanitize=address`, own `build/linux-asan` dir) is the tool for crashes that only reproduce in the full ordered suite (teardown and store use-after-frees). Sync the tree with `remote-build.ps1 -Target linux -NativeQt -SkipTests`, then build and run the instrumented suites on the box:

    ssh <linux-build-host> "ASAN_OPTIONS='quarantine_size_mb=8192:malloc_context_size=25:detect_leaks=0' \
      bash ~/patchy/src/scripts/remote/build-and-test.sh linux-asan"

The large quarantine keeps long-ago frees poisoned for the whole run, so the host needs plenty of RAM; `detect_leaks=0` keeps exits quiet. The UI suite's POSIX SIGSEGV/SIGBUS reporter steps aside under ASAN (`tests/ui/main.cpp`). A fresh build dir also surfaces stale `test-artifacts` expectations long-lived dirs hide ([testing.md](testing.md#running-and-filtering)).

## Color scheme and native chrome

`ThemeManager` mirrors the resolved scheme onto Qt with `QStyleHints::setColorScheme` (`unsetColorScheme` for "follow system"). On Windows that re-tints the chrome Patchy does not style (native dialog title bars, dock and list scroll bars, tooltips, `QMessageBox`, the color picker), so there is no `DWMWA_USE_IMMERSIVE_DARK_MODE` call and no registry read. Under `QT_QPA_PLATFORM=offscreen` the call is a no-op and `colorScheme()` reports `Unknown`, which keeps the UI suite deterministic regardless of the host's setting.

When the platform reports `Unknown` (no desktop color scheme: a bare X server, a window manager without a portal), "follow system" resolves to Light on Linux under the `xcb` and `wayland*` platforms and to Dark everywhere else, offscreen on Linux included, so the Linux UI suite keeps its Dark baselines. The AppImage catalog's test runs under a bare X server and warned about a mostly dark window (GitHub issue 334). `tests/ui/main.cpp` pins the suite to Dark anyway; `ui_color_scheme_unknown_resolves_light_on_desktop_linux` covers the mapping on every platform.

The frameless main window has no native title bar to tint; it draws its own 1px edge from the `window_border` role, overridden by the Light palette so it keeps a silhouette on a light desktop. On Windows 11 DWM's 1px border is drawn over that client edge, so `apply_window_border_color` (`window_effects.cpp`) gives it its own role, `window_outer_edge`: lighter than every chrome surface along the window's edges in a dark theme (title bar, tool palette, options bar, panels), darker in a light one, so the window reads as framed on a black or white desktop. It applies to the main window and every self-framed dialog, active and inactive alike, and again on a theme switch (`apply_color_scheme`, a dialog's `StyleChange`); `ui_frameless_window_keeps_caption_and_themed_border` checks the contrast for Dark, Light and every bundled theme. Never `COLOR_NONE` or `window_border` (no visible edge on a dark desktop, GitHub issue 674) and never the default (it turns near white when the window loses focus). Windows 10 ignores the attribute; the client-drawn edge is its only one.

The window controls sit on top of the menu bar's right end, so an empty top-right corner widget (`windowChromeControlsReserve`, sized in `position_window_chrome_controls` to end where the controls begin; QSS `padding-right` does not narrow QMenuBar's title area) keeps their width free: a narrow window moves the titles that do not fit into QMenuBar's own overflow button (`qt_menubar_ext_button`, never title-bar drag area) instead of hiding them under the controls (`ui_narrow_menu_bar_overflows_before_window_controls`). QMenuBar keeps the rects of the titles it moved into that menu and `actionAt` still returns them, so `title_bar_drag_area_contains` counts a title only on the shown side of the button; the blank bar either side of it drags (GitHub issue 685, `ui_narrow_menu_bar_blank_beside_overflow_drags_window`).

The main window keeps a full native frame (`frameless_main_window_style`: `WS_CAPTION`, `WS_SYSMENU`, `WS_THICKFRAME`, both size boxes) and `WM_NCCALCSIZE` hides all of it, pinning a zoomed window's client to the work area. Windows animates minimize, maximize and restore only for a captioned window, so the window buttons send `WM_SYSCOMMAND` (`SC_MINIMIZE`, `SC_MAXIMIZE`, `SC_RESTORE`) like the native caption buttons; Qt follows through `WM_SIZE`, as for a title-bar double-click or Win+Up. A window Qt maximized itself (the start-up restore) is not natively zoomed, so its restore stays on `restore_window_from_maximize`. `WM_NCACTIVATE` still passes `lParam` -1 so the hidden caption is never repainted.

## Linux base widget style

`AppStyle` wraps whatever style Qt picks from the platform theme's style list. The Flatpak on KDE gets Breeze (org.kde.Platform ships the Breeze style and KDE's platform theme); on other desktops it gets Fusion. The AppImage's Qt kit has only Windows and Fusion, and every theme lists Fusion ahead of Windows (KDE's after its configured Breeze), so it gets Fusion everywhere. The app stylesheet draws nearly every control, so the base style shows only in small parts (the arrow of a combo box the QSS leaves unstyled, menu mnemonic underlines). Following the desktop style for the rest would mean dropping the stylesheet: a design choice, not a fallback fix.

## Styled QCheckBox and QMacStyle margins

A stylesheet-styled QCheckBox needs a NON-native border in some matching rule. The app stylesheet's global `QCheckBox { border: none; }` covers this; do not remove it. Qt only suppresses QMacStyle's Aqua layout-item margins (checkboxes: +2,+3,-9,-4) for styled widgets whose rule has a non-native border (qstylesheetstyle.cpp, SE_*LayoutItem). With the margins active, box layouts overlap the neighboring label ~9px into the checkbox: right for the inset native glyph, wrong for the flat 12px stylesheet indicator (the label lands ON the box). Only reproducible in the real app: the test harness never loads QMacStyle.

## macOS menubar submenus and Qt's menu-role heuristic

Qt's Cocoa plugin merges an item directly under a top-level menu into the app menu when its title starts with the translated "About", "Config", "Preference", "Options", "Setting", "Setup", "Quit" or "Exit", re-checked on every sync, so a language switch flips items ("Ajustes", "Réglages"). A merged SUBMENU crashes Qt 6.8.3 on the next window activation (`setSubmenu:` on its freed old `NSMenuItem`; GitHub issue 29). Every menubar submenu action gets `QAction::NoRole` (`exclude_submenus_from_native_menu_roles` and the dynamic submenu builders; `ui_menubar_submenus_have_no_native_menu_role` guards it, `ui_language_switch_survives_window_reactivation` under `PATCHY_UI_TEST_PLATFORM=cocoa NSZombieEnabled=YES` reproduces the crash).

## macOS synthesized font stretch (Photoshop HorizontalScale)

Qt 6.8's CoreText engine applies `QFont::setStretch` to advances TWICE: the stretch is baked into the CTFont matrix and `loadAdvancesForGlyphs` multiplies the already-scaled advances by it again (qfontengine_coretext.mm), so stretch 90 rendered at 81% on macOS. DirectWrite/GDI and FreeType apply it once. `set_stretch_for_advance_ratio` (text_document.cpp) therefore picks between the plain `round(ratio x 100)` encoding and the sqrt-compensated one by MEASURING the font at hand (which candidate's 'H' advance lands the requested ratio). Per font, not per platform or process: a font with a native width axis answers `setStretch` through real width matching (`.AppleSystemUIFont` measured x0.39 at stretch 50, neither linear nor squared, so no per-process probe can stand in), and a missing family's fallback engine ignores stretch (the measurements tie, the plain value stays). On linear engines the plain candidate wins, so Windows/Linux pixels are unchanged; a Qt release fixing CoreText flips macOS back by itself, and the SNES box probes pin the result. Accepted residue: macOS glyph OUTLINES render sqrt(ratio) wide (the matrix half); advances, line breaks and ink extents are what Photoshop parity needs.

## macOS non-modal dialog anchoring

On macOS, `run_non_modal_dialog` anchors the dialog as a native child window of its parent widget's window (`keep_dialog_above_parent_window`, dialog_utils_mac.mm): macOS has no Win32 owned-window z-order, so clicking the edit-locked main window would bury the dialog behind it. The anchor attaches on Show (deferred one event-loop turn) and MUST detach on Hide/Close because AppKit re-orders attached children with their parent even when hidden. Any new non-modal dialog path that bypasses `run_non_modal_dialog` needs the same call.
