# Platform notes (macOS/Linux ports)

Deep reference for cross-platform work. The cross-cutting rules (lead-platform policy, `#ifdef` conventions, the `use_custom_window_chrome()` gate, triage order, byte-identical serialization) live in AGENTS.md's "Platform portability" section; read this before hunting a platform-specific regression or adding a platform-guarded site.

## Remote build machinery

macOS (arm64, preset `mac-release`, Qt at `.deps/Qt/6.8.3/macos`) and Linux (preset `linux-release`, Qt at `.deps/Qt/6.8.3/gcc_64`) build remotely via `scripts\remote\remote-build.ps1 -Target mac|linux`, which snapshots the working tree (uncommitted changes included; it creates no commits or branches and does not touch the real index) to a bare repo on `seth@studiomac.local` / `glados@glados.local`, builds there, and runs both suites (core + offscreen UI) with output streamed back. One-time machine provisioning is `scripts/remote/setup-mac.sh` / `setup-linux.sh` (idempotent: venv tools + Qt via aqtinstall + apt deps).

## Platform-specific site inventory (keep current)

- `main_window_chrome.cpp` + the `use_custom_window_chrome()` call sites in `main_window.cpp` (frameless flag, chrome controls).
- `psd_document_io.cpp` DirectWrite font resolution + wide-string helpers (portable heuristic fallback).
- `layer_list_widget.cpp` drag-wheel low-level mouse hook (degrades gracefully).
- `dialog_utils.cpp` `use_qt_file_dialog_controls` (Qt dialog widgets only under offscreen; native/portal dialogs otherwise, on every OS).
- `dialog_utils_mac.mm` `keep_dialog_above_parent_window` (macOS child-window anchor for non-modal dialogs — see the non-modal dialog rules in AGENTS.md; no-op elsewhere; first Objective-C++ TU, `enable_language(OBJCXX)` is APPLE-gated in CMakeLists).
- The app stylesheet's `QCheckBox { border: none }` (macOS Aqua layout-item margin suppression — see the setItemWidget gotcha in AGENTS.md) and its APPLE-gated `QGroupBox` block + `brush_dynamics_popup.cpp` `compact_group_grid` (QMacStyle's Aqua group-box chrome and layout spacings blow dense panels past the screen; Windows keeps native metrics).
- `dialog_utils.cpp` `suppress_native_tab_bar_base` (macOS document-mode tab bars paint a light native base across their width — the ::tab rules still apply — so the document tabs and Preferences tabs drop the base; no-op elsewhere).
- `main.cpp` `InteractionHintsStyle::styleHint` macOS block (pins SH_FormLayoutFieldGrowthPolicy / LabelAlignment to the Windows behavior — QMacStyle otherwise keeps form fields at size-hint and right-aligns labels, shrinking Name/Folder-style edits to slivers) and the APPLE-gated QScrollBar block in `photoshop_style()` (Windows-classic dithered track via scroll-dither.svg, flat bordered handle, deliberately NO arrow buttons — fixed-size QSS line buttons make the groove degenerate on short scrollbars in collapsed docks; QMacStyle's flat overlay bars hide the handle on the dark theme).
- `update_checker.cpp` platform id (windows/macos/linux manifest keys).
- `main.cpp` Windows app-font candidates + macOS `Contents/Resources` probes (with `localization.cpp`'s translations probe).
- `main_window_palette.cpp` uses `toStdU16String()` for `std::filesystem::path` (UTF-16 -> native on every platform — do not reintroduce `toStdWString`).
- Tests: `test_harness.hpp`, the paired crash reporters in `ui_visual_tests.cpp`, `test_fonts.hpp`.

## Styled QCheckBox and QMacStyle margins

A stylesheet-styled QCheckBox needs a NON-native border in some matching rule — the app stylesheet's global `QCheckBox { border: none; }` covers this; do not remove it. Qt only suppresses QMacStyle's Aqua layout-item margins (checkboxes: +2,+3,-9,-4) for styled widgets whose rule has a non-native border (qstylesheetstyle.cpp, SE_*LayoutItem). With the margins active, box layouts deliberately overlap the neighboring label ~9px into the checkbox — right for the inset native glyph, but on the flat 12px stylesheet indicator the label lands ON the box (the 0.13-mac "text jammed into the checkbox" Layer Style bug). Only reproducible in the real app: the test harness never loads the QMacStyle plugin, so offscreen/test runs cannot catch a regression here.

## Per-platform test skips (keep this list current)

On macOS/Linux: `ui_bundled_legacy_plugin_action_applies_filter` and `ui_transparency_checkerboard_and_copy_paste_preserve_alpha` (Windows-only bundled legacy 8BF shims; the contact sheet drops their three artifacts), `ui_frameless_window_edges_resize` (native frame owns resize borders; gated on `use_custom_window_chrome()`), and the two `ui_imported_psd_box_text_line_clip_*` tests (they pin Windows Arial line metrics; CoreText/fontconfig lay lines out a few px differently). Seven imported-PSD raster-preview text tests gate on **installed Arial** via `skip_without_arial_for_psd_text_preview()` (Linux ships Liberation, not Arial; without the face the Missing Font prompt correctly appears, which offscreen cannot answer — the suite HANGS in the nested dialog loop, it does not fail). `ui_main_window_renders_color_controls` asserts frameless/badge/window-buttons **presence on Windows and absence elsewhere**. Local-fixture (`local-test-fixtures/`) tests `[SKIP]` on the remotes because that directory is deliberately untracked.

## macOS non-modal dialog anchoring

On macOS, `run_non_modal_dialog` anchors the dialog as a native child window of its parent widget's window (`keep_dialog_above_parent_window`, dialog_utils_mac.mm): macOS has no Win32 owned-window z-order, so clicking the edit-locked main window buried the dialog behind it and the app looked frozen (0.13 mac bug). The anchor attaches on Show (deferred one event-loop turn) and MUST detach on Hide/Close — AppKit re-orders attached children with their parent even when hidden. Child windows follow parent moves; that is accepted mac-native behavior. Any new non-modal dialog path that bypasses `run_non_modal_dialog` needs the same call.
