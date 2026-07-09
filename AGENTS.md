# Repository Instructions

Keep this file current: when a change makes anything in here stale (build steps, test conventions, gotchas), update it in the same change. Multiple different coding agents read this file — it is the shared knowledge channel, so write notes for any AI, not a specific one.

When bumping the release version, update the version fields: `CMakeLists.txt` (`project(... VERSION x.y)`), `vcpkg.json` (`version-semver`), and `latest_version.json` (the per-platform `version` entries — windows always; macos/linux only when those artifacts actually ship, since this is the update-check manifest served to the app from raw.githubusercontent.com on main and only takes effect once pushed). Also bump the `<release>` tag in `packaging/linux/com.rtsoft.patchy.metainfo.xml`, and add a new top entry under `README.md`'s "What's New" section for that version, dated with the release date, summarizing the user-visible changes. Build order matters: finalize the README first (the Windows zip/installer embed a copy), then `release-all.bat` (three consoles: Windows + remote mac/linux; every builder deletes its previous artifacts up front so a failed build can never leave stale files for the newest-file upload scripts), then `upload-to-rtsoft.bat`.

Always credit the correct author on each "What's New" bullet. Seth is the default and is left uncredited; any feature or fix contributed by someone else must name them with a GitHub handle link like `([@handle](https://github.com/handle))`. Check `git log`'s author for the commits behind each bullet (e.g. `git log --format='%an %s'`) rather than assuming, and when one bullet mixes work from more than one person, credit the specific clause that person wrote (see the existing 0.10/0.12 entries for the mid-bullet style).

When adding or changing user-facing English text, make sure it is wired through Patchy's localization system and update the Japanese translation in `translations/patchy_ja.ts` in the same change.

Keyboard shortcuts for QActions must be registered through `MainWindow::register_hotkey(action, "stable.id", default_seq)` (backed by `HotkeyRegistry` in `src/ui/hotkey_registry.hpp`) — never call `setShortcut`/`setShortcuts` directly on an app-level action. The registry applies user customizations from the `hotkeys/` settings group (only deltas are stored; unmodified commands track new defaults automatically) and feeds the Preferences > Hotkeys editor. Command ids are persisted in user settings, so never rename one. Two commands must not share a default shortcut — Qt silently deactivates ambiguous application shortcuts; the `ui_hotkey_defaults_have_no_conflicts` visual test enforces this.

If tests need files from outside the project directory, copy those files into `local-test-fixtures` first and have the tests read them from there. Do not add hardcoded external drive paths such as `C:\temp` or `D:\projects` to test code.

Keep git commit messages to one or two lines — a concise subject, no multi-paragraph body enumerating every change.

In user-facing documentation (README.md, release notes, website copy), never use em-dashes; use a normal dash, comma, colon, or parentheses instead. More generally, avoid writing that smells AI-generated: words like "seamlessly", "robust", "comprehensive", "delve", hype adjectives, "not just X, but Y" constructions, and emoji-decorated headings. Seth finds it off-putting; write plain, direct, human-sounding prose.

## Platform portability (Windows lead; macOS/Linux ports)

Windows is the lead platform and must never regress: the release handoff at the bottom of this
file stays mandatory for every code change. macOS (arm64, preset `mac-release`, Qt at
`.deps/Qt/6.8.3/macos`) and Linux (preset `linux-release`, Qt at `.deps/Qt/6.8.3/gcc_64`) build
remotely via `scripts\remote\remote-build.ps1 -Target mac|linux`, which snapshots the working
tree (uncommitted changes included; it creates no commits or branches and does not touch the
real index) to a bare repo on `seth@studiomac.local` / `glados@glados.local`, builds there, and
runs both suites (core + offscreen UI) with output streamed back. One-time machine provisioning
is `scripts/remote/setup-mac.sh` / `setup-linux.sh` (idempotent: venv tools + Qt via
aqtinstall + apt deps). Changes that touch platform-guarded code, CMakeLists/presets, or
packaging should run the affected remote build(s) best-effort and report the result; pure
Windows-only work need not.

Conventions for platform-specific code:

- Default to a small, local `#ifdef Q_OS_WIN / Q_OS_MACOS / Q_OS_LINUX` (`_WIN32` in Qt-free
  code) **with a portable fallback**. Split into a per-OS translation unit (`foo_win.cpp` /
  `foo_mac.mm` / `foo_linux.cpp` behind `WIN32`/`APPLE`/`UNIX AND NOT APPLE` in CMakeLists, one
  shared header) only when a site needs Objective-C++/system frameworks or outgrows about a
  screenful; per-OS files live next to their feature, not in a platform/ directory.
- Window-frame code stays concentrated in `main_window_chrome.cpp`.
  `MainWindow::use_custom_window_chrome()` is the single gate: true only on Windows (frameless
  window + custom title-bar controls + Qt-level edge-resize machinery); macOS/Linux use the
  native frame, and on macOS the default QMenuBar becomes the global menu bar — never call
  `setNativeMenuBar(false)` outside the gated `configure_window_chrome()`.
- Tests obtain font files only through `tests/test_fonts.hpp` (role-based, per-OS candidate
  paths; the Windows lists are the exact historical ones so Windows baselines never move).
  Mac/Linux test-failure triage: fix real bugs > per-platform skip-with-reason > per-platform
  baseline; never loosen a tolerance globally to make another platform pass.
- Platform-specific site inventory (keep current): `main_window_chrome.cpp` + the
  `use_custom_window_chrome()` call sites in `main_window.cpp` (frameless flag, chrome
  controls); `psd_document_io.cpp` DirectWrite font resolution + wide-string helpers (portable
  heuristic fallback); `layer_list_widget.cpp` drag-wheel low-level mouse hook (degrades
  gracefully); `dialog_utils.cpp` `use_qt_file_dialog_controls` (Qt dialog widgets only under
  offscreen; native/portal dialogs otherwise, on every OS); `dialog_utils_mac.mm`
  `keep_dialog_above_parent_window` (macOS child-window anchor for non-modal dialogs — see the
  non-modal dialog section below; no-op elsewhere; first Objective-C++ TU, `enable_language(OBJCXX)`
  is APPLE-gated in CMakeLists); the app stylesheet's `QCheckBox { border: none }` (macOS Aqua
  layout-item margin suppression — see the styled-checkbox note in the setItemWidget section) and
  its APPLE-gated `QGroupBox` block + `brush_dynamics_popup.cpp` `compact_group_grid` (QMacStyle's
  Aqua group-box chrome and layout spacings blow dense panels past the screen; Windows keeps
  native metrics); `dialog_utils.cpp` `suppress_native_tab_bar_base` (macOS document-mode tab
  bars paint a light native base across their width — the ::tab rules still apply — so the
  document tabs and Preferences tabs drop the base; no-op elsewhere); `main.cpp`
  `InteractionHintsStyle::styleHint` macOS block (pins SH_FormLayoutFieldGrowthPolicy /
  LabelAlignment to the Windows behavior — QMacStyle otherwise keeps form fields at size-hint
  and right-aligns labels, shrinking Name/Folder-style edits to slivers) and the APPLE-gated
  QScrollBar block in `photoshop_style()` (Windows-classic dithered track via scroll-dither.svg,
  flat bordered handle, deliberately NO arrow buttons — fixed-size QSS line buttons make the
  groove degenerate on short scrollbars in collapsed docks; QMacStyle's flat overlay bars hide
  the handle on the dark theme);
  `update_checker.cpp` platform id (windows/macos/linux manifest keys); `main.cpp` Windows
  app-font candidates + macOS `Contents/Resources` probes (with `localization.cpp`'s translations
  probe); `main_window_palette.cpp` uses `toStdU16String()` for `std::filesystem::path` (UTF-16 →
  native on every platform — do not reintroduce `toStdWString`); tests (`test_harness.hpp`, the
  paired crash reporters in `ui_visual_tests.cpp`, `test_fonts.hpp`).
- File formats must stay byte-identical across platforms: PSD I/O is explicit big-endian
  fixed-width (`psd_binary.hpp`); keep any new serialization that way (no `size_t`/`long`/
  `wchar_t` writes, no raw struct dumps).

## MainWindow's implementation is split across main_window_*.cpp

`MainWindow` is still one class (`src/ui/main_window.hpp`), but its implementation spans several
translation units (July 2026, splitting the old 22.5k-line main_window.cpp). Put new member
functions in the file that owns their area, and keep the split pure: moving a function between
these files must never change its body.

- `main_window_chrome.cpp` — frameless-window machinery: `nativeEvent`, resize borders/cursors,
  maximize/restore, window-geometry persistence, and `use_custom_window_chrome()` (the
  platform gate). This file deliberately concentrates the `Q_OS_WIN` window-frame code; other
  platform-specific sites are inventoried in the "Platform portability" section above.
- `main_window_palette.cpp` — palette-mode document mutations, palette file I/O, panel/chip
  refresh, compliance scan (see the palette-mode section below).
- `main_window_adjustments.cpp` — Filter menu apply flow, Levels/Curves/Hue-Sat/Color-Balance
  dialogs, adjustment-layer create/preview/edit, async pixel-preview machinery.
- `main_window_shared.{hpp,cpp}` — helpers used by more than one of these TUs. The per-file
  helpers live in each TU's anonymous namespace; when a second TU needs one, MOVE it here and
  declare it in the header (never copy it — a duplicated helper with a static local forks its
  state, and an extern declaration alongside a same-name anonymous-namespace definition makes
  every call ambiguous). The split TUs repeat main_window.cpp's full include block; that is
  deliberate (harmless, and keeps extraction mechanical).

To continue the split (create_actions/docks, documents+file I/O+recents, layers, clipboard,
settings are the remaining clusters): cut whole contiguous top-level definitions, wrap them in
`namespace patchy::ui { namespace { ... } ... }`, add the file to `patchy_ui` in CMakeLists.txt,
and let compile errors enumerate the helpers to move or promote. Count braces at the cut edges
(an off-by-one that eats a `struct X {` line closes the anonymous namespace early and shows up
as baffling "ambiguous overload" errors far away). Verify with the full UI suite afterwards.

Do NOT attempt the text tool as a pure-move split: the text render pipeline
(runs<->QTextDocument converters, `render_text_layer_pixels_*`, text metadata store/clear,
caret/preview helpers, the `kTextEditor*` property-key constants) is shared between the inline
editor members, `configure_canvas`'s text re-render callback, `rasterize_active_layers`, and
`eventFilter`. It was tried and backed out (July 2026): ~20 symbols would need promotion, i.e.
it is really a "design a text_render module with its own header" job, not a file split.

## Single-instance: command-line files reuse the running window

Patchy is single-instance (`src/app/main.cpp`). On launch it resolves the positional file args to
absolute paths, then tries to connect to a per-user `QLocalServer` named
`Patchy-SingleInstance-<USERNAME>`. If a Patchy is already running it serializes the file list over
the socket (a `QStringList` via `QDataStream`, version pinned to `Qt_5_15`) and exits `0`; the running
instance reads the payload on the connection's `disconnected` signal, then `MainWindow::activate_for_second_instance`
un-minimizes/raises/activates the window and opens the files. So double-clicking a file in Explorer
opens it in the existing window and brings it to the foreground instead of spawning a new process.

The first launch becomes the server (`removeServer` first clears a pipe left by a prior crash). This
needs Qt6::Network, already linked via `patchy_ui`. Set `PATCHY_NO_SINGLE_INSTANCE` to allow multiple
instances (tests and other launches are unaffected since they construct `MainWindow` directly, not via
`main()`).

The same channel carries a screenshot command so agents can SEE the real running app without
desktop-automation tools: `patchy.exe --screenshot <out.png>` saves a grab of the running
instance's window (add `--screenshot-widget <qtObjectName>` for one child widget, e.g.
`layerList`, and/or `--screenshot-rect x,y,w,h` for a crop) without raising or focusing it.
The invoking process exits immediately; poll for the output file. Combine with positional
files to open a document first (opens DO activate the window). With no instance running, the
new process shows the window, captures ~1.5s after startup, and exits (0 saved / 3 failed).
Backed by `MainWindow::save_debug_screenshot`; the command rides the forwarded `QStringList`
as one reserved `patchy-cmd:screenshot\n...` entry (real entries are absolute paths, so no
collision). Coverage: `ui_debug_screenshot_saves_window_widget_and_region`.

## Document-level alpha imports as an editable layer mask

A flat image's per-pixel alpha (a 32-bit BMP — including `BI_RGB`/compression 0, whose 4th
byte Patchy now keeps — or a PNG/TIFF alpha, or a flat PSD's extra channel) is imported as
an editable grayscale **layer mask** rather than as pixel transparency, matching how
Photoshop shows the alpha of an opaque flattened image as a saved "Alpha 1" channel. The
shared step is `patchy::ui::promote_flat_alpha_to_layer_mask` (called once in
`load_document_from_path`); it only fires for a single flat pixel layer and skips uniform
alpha (all-255 = nothing to mask, all-0 = treated as opaque, never fully masked).

- Such masks are tagged with layer metadata `kLayerMetadataDocumentAlpha`
  (`layer_mask_is_document_alpha`). This marker is what distinguishes an imported
  document-alpha channel from a hand-authored layer mask — only marked masks are written
  back as the file's alpha. Hand-authored layer masks still save as PSD layer masks. The
  marker is re-derived on every load (it is not persisted in the file).
- On save, a marked single-layer doc round-trips the mask as the file's alpha
  **non-destructively** (the colors under the mask are preserved, not erased): BMP 32-bit,
  PNG/TIFF/WebP via Qt, and PSD as a document-level "Alpha 1" channel (header channel count
  4 + image resource 1006 naming it "Alpha 1"; the per-layer mask is suppressed in the
  layered writer so Photoshop shows an opaque layer + the named channel, not both). The
  shared "keep RGB, mask becomes straight alpha" buffer is `document_alpha_rgba8`
  (`core/layer_render_utils`); PSD has a parallel `document_alpha_composite`. Compositing
  (`render_rgba8` / `qimage_from_document`) is the destructive path and is only used for
  unmarked docs.
- Round-trip coverage: `ui_flat_alpha_round_trips_as_editable_mask` in `ui_visual_tests.cpp`.

## Palette / indexed-color editing mode (constrained RGBA, never indexed storage)

Palette mode (Image > Mode > Indexed (Palette), July 2026) gives pixel artists a
low-color workflow WITHOUT changing pixel storage: pixels stay RGBA everywhere and the
mode is a per-document write constraint. Do not "upgrade" this to true index-per-pixel
storage; that would ripple through the compositor, brush engine, styles, and PSD I/O
(the reason Photoshop's own indexed mode flattens and disables features).

- **Model**: `Document::palette_editing()` (`DocumentPaletteEditing{Palette, alpha_threshold,
  palette_revision}` in core/document.hpp). Present = mode on. `indexed_palette()` stays what it
  always was: an attached palette WITHOUT the constraint (imports, RGB round trips). Every palette
  mutation must bump `palette_revision` with an app-globally unique value
  (`MainWindow::next_palette_revision()`, never reused, because the canvas LUT cache treats equal
  revisions as identical palettes) and re-run `sync_document_indexed_palette` so indexed BMP/PNG
  export sees the editing palette. Undo snapshots copy the whole Document, so palette + pixel
  changes are one undo step; no extra machinery.
- **Write semantics** (core/palette.hpp): coverage below `PaletteSnapContext::coverage_threshold`
  (0.5) writes nothing, otherwise the blend runs at full strength, RGB snaps to the palette
  (exact-membership check FIRST, then a 15-bit LUT; the exact check keeps near-duplicate entries
  like the NES blacks idempotent), and alpha hardens to 0/255. Mode off = null pointer = the
  historical write path bit for bit, pinned by `tool_write_paths_digest_baseline` (re-pin its
  constants from the failure output only for deliberate rendering changes).
- **The five write sites** (a new pixel-writing path must add the same snap): core `write_pixel`
  (wrapper over `write_pixel_blend`; covers brush dabs, shapes, fill_rect, clear_rect, gradient),
  `flood_fill` (snaps its replacement color up front), `smudge_brush_segment`, and canvas-side
  `CanvasWidget::write_brush_stroke_pixel_from_snapshot` (the COMMON brush path via
  `EditOptions::stroke_pixel_writer`, which bypasses write_pixel) plus `clone_brush_segment`. The
  hook rides `EditOptions::palette_snap`, attached in the canvas `edit_options` factory
  (`CanvasWidget::palette_snap_for_edits`, null while editing a layer MASK). FG/BG colors snap at
  `set_primary_color`/`set_secondary_color` while the mode is on.
- **Display WYSIWYG**: in palette mode the canvas composite is quantized for DISPLAY
  (`CanvasWidget::quantize_image_for_palette_display`, applied at every render_cache_ producer:
  the two full-refresh sites, the async refresh, the flat-composite path, and
  `patch_render_cache_patches`), so live layer styles / text / blend modes LOOK exported while
  staying editable; converting to RGB shows their true colors again. Any operation that changes
  the palette or the mode flag must call `canvas_->document_changed()` or the display keeps the
  stale quantization (`ui_palette_mode_display_quantizes_layer_styles` pins both directions).
  Layer pixels, the eyedropper-to-FG snap, and exports are unaffected by the display path.
  Because the display quantization can hide off-palette LAYER PIXELS, Image > Mode > RGB Color
  prompts when the document is not palette-clean (`convertToRgbKeepLookMessageBox`): "Keep
  Palettized Look" bakes the displayed snap into the pixels (`apply_palette_to_layers`, dither
  None, the editing alpha threshold — the same math as the display quantization, i.e. Snap Image
  to Palette) in the same undo step as the mode change; "Restore Original Colors" is the
  historical lossless exit. Clean documents never prompt (identical either way). Composite-time
  contributors (live styles, blend modes, text AA against lower layers) still return to full
  color on exit — only a flatten could freeze those, deliberately out of scope
  (`ui_convert_to_rgb_prompts_to_keep_palettized_look` pins all three paths).
- **Advisory, not blocking**: filters, adjustment/layer styles, text AA, and paste stay usable and
  can produce off-palette pixels (hidden by the display quantization; surfacing them is what the
  RGB-convert prompt above is for); a debounced
  (~400 ms, skipped over 4 Mpx) scan flags the status
  chip and Image > Snap Layer/Image to Palette fixes them
  (`apply_palette_to_pixels`, dither is convert-time only). Editing a palette entry remaps by
  EXACT color match (`remap_document_exact_color`), lossless because enforced pixels are exact
  palette colors; reordering entries needs no pixel change at all (pixels reference colors by
  value, not index). Preset additions (July 2026): "vga256" (246 colors — the IBM VGA mode-13h
  default DAC table generated constexpr from ramps verified against DOSBox's vga_palette, minus
  the gray-ramp endpoints and trailing blacks that duplicate other entries) and "dink" (the
  256-color Dink Smallwood game palette in engine index order, extracted from RTsoft's own
  8-bit art; pure green #00FF00 near the end is the engine's sprite key, deliberate).
  `palette_presets_are_well_formed` (test_main.cpp) pins counts, spot colors, and the
  no-duplicates rule for every preset.
- **Persistence**: PSD image resource id **4210** (plug-in range; payload documented at
  `kImageResourcePatchyPalette` in psd_document_io.cpp). The header stays RGB, so Photoshop and
  pre-feature Patchy open the file as a plain RGB PSD and round-trip the resource verbatim.
  Malformed payloads are ignored. Written for attached-but-inactive palettes too (flags bit0 = mode).
- **I/O**: palette files read via content sniffing in `formats/palette_io.*` (JASC/RIFF .pal,
  .gpl, .hex, .act, .aco, .ase, indexed .bmp; writers for .pal/.gpl/.hex/.act/.aco); the BMP
  export "palette file" mode shares it. PNG export of a palette-mode document automatically
  writes indexed PNG-8 with the palette in file order (+ one tRNS slot when transparency exists
  and the table has room); convert to RGB mode first for a plain RGBA PNG. Opening an indexed
  BMP/PNG/GIF offers to adopt its palette (`imports/adoptIndexedPalette` setting: ask/always/never).
- **UI**: `src/ui/palette_panel.*` (dock, passive view; MainWindow owns every mutation +
  snapshot + refresh), `src/ui/palette_convert_dialog.*` (source/dither/threshold; returns the
  RESOLVED palette), status chip `paletteModeChip`, and the
  `image.mode_rgb`/`image.mode_indexed`/`image.snap_*` hotkey ids. Panel interactions: click
  selects + sets FG (and pushes the color into any open color picker — the transient
  `request_patchy_color` dialog via `apply_color_to_open_color_picker`, which fires its live
  callback so e.g. a layer-style color updates, plus a signal-blocked mirror into the persistent
  foreground/text `color_dialog_`) and the readout label shows "Index N: #hex" with a Copy
  button (`paletteCopyHexButton`, also in the context menu) that emits `copy_color_requested`;
  dragging a swatch onto another SWAPS the two entries (no pixel change, it only reorders export
  indexes); Edit > Copy/Paste act on the selected swatch as "#rrggbb" text while keyboard focus
  is inside the panel, routed at the top of `copy_selection()`/`paste_clipboard()` (a parallel
  panel QShortcut would make Ctrl+C/V ambiguous with the app actions and Qt would fire neither).
  The convert dialog's preview is a zoom/pan widget (`paletteConvertPreview`: fit by default,
  wheel/buttons/double-click zoom to 16x, drag pans, state exposed via `previewZoomPercent`/
  `previewFitMode` dynamic properties): the debounced refresh converts a bounded ≤640px overview,
  and past that resolution the widget converts an exact full-res window on demand (whole image
  when ≤2 Mpx so Floyd-Steinberg patterns never shift while panning; larger docs use an
  8-aligned visible window — Bayer matrices index buffer-local coords — and mid zooms whose
  window would exceed the budget fall back to the smoothed overview). Applying the conversion
  shows the standard busy `QProgressDialog` (`paletteConvertProgressDialog`) past 250k layer
  pixels, covering the undo snapshot + per-layer rewrite.
- **The color picker's swatch column is palette-driven** (`color_panel.cpp`): a dropdown
  (`patchyColorPaletteCombo`: Basic colors / Current palette / [File: name] / built-in presets,
  then Load Palette File... / Save Palette As... action rows) feeds a custom-painted, scrollable
  swatch grid (`patchyColorPaletteGrid`, adaptive columns, cells shrink past 64 entries).
  MainWindow::refresh_palette_panel publishes the document palette to every open picker via
  `set_color_picker_document_palette` (file-static state + picker registry in color_panel.cpp);
  palette mode turning on switches open pickers to "Current palette", which is also the default
  while the mode is active. Outside palette mode the picker opens on the remembered choice — the
  `palettes/lastPaletteChoice` settings key (`kColorPickerPaletteChoiceKey`), written by user
  dropdown changes AND by the Palette panel's preset menu, so the two stay in sync. Programmatic
  combo switches are signal-blocked; an unblocked `currentIndexChanged` is BY DESIGN treated as
  a user choice and persisted. The action rows never stay selected (the handler defers the file
  dialog one turn, then either selects the lazily-inserted "file" state row or restores
  `last_real_palette_choice_`); a loaded file persists by PATH (`palettes/lastPaletteFile`) and
  reloads quietly on the next picker, falling back to basics if it vanished. The load/save
  dialogs are the shared `prompt_load_palette_file`/`prompt_save_palette_file`/
  `read_palette_file_quietly` in palette_panel.cpp — MainWindow's panel Load/Save delegate to
  the same functions (one filter list, one `palettes/lastDirectory` memory).
  Colors drag & drop with Qt's standard color mime + "#RRGGBB" text (`start_color_drag`/
  `color_from_mime`): the palette grid and the current-color preview (`ColorPreviewFrame`) drag
  out, custom slots (`ColorWellButton`) drag out and accept drops (overwrite + select), and the
  preview accepts drops (sets the current color). The palette grid also has a SELECTED cell
  (`paletteSelectedIndex` property, set by click) and is itself a drop target when the shown
  palette is EDITABLE (`palette_choice_is_editable`): the loaded file palette edits in memory
  (unsaved until Save Palette As...), and the current palette routes through MainWindow's
  `set_color_picker_document_palette_editor` hook → `apply_palette_entry_color` (undoable,
  refreshes panel + pickers; the hook is cleared in ~MainWindow). Basic colors and built-in
  presets are read-only — grid drops are rejected for them. Edit > Cut/Copy/Paste route to the
  picker while keyboard focus is inside one — `color_picker_ancestor_of(QApplication::focusWidget())`
  at the TOP of MainWindow::cut_selection/copy_selection/paste_clipboard, exactly like the
  Palette panel routing below them (never add a parallel QShortcut: ambiguous with the
  application-context hotkeys, Qt fires neither). Copy = current color; Paste targets where the
  user is working — a FOCUSED custom slot, else the focused grid's selected editable cell, else
  the current color; Cut = the selected custom slot's color + resets that slot to white (no
  slot selected = Copy). The single "Set Custom Color" button (patchySetCustomColorButton,
  disabled until a slot is selected) writes the current color into the selected slot — it
  replaced the old Add/Update pair; drops and paste fill slots without any selection. The
  picking surfaces and swatches take ClickFocus for exactly this routing; text fields keep
  native clipboard handling via ShortcutOverride. The preview-dialog
  edit lock's blanket DnD block (`handle_layer_action_button_drag_event`) exempts color-mime
  drags — they never touch the document, and the layer-style picker's slots must stay droppable
  mid-preview. Synthetic-drop test gotcha: Qt discards a bare QDropEvent; send a QDragEnterEvent
  first (that is what registers the drop target QApplication routes the Drop to).
  Coverage: `ui_color_picker_palette_dropdown_tracks_mode_and_choice`,
  `ui_color_picker_file_palette_clipboard_and_drop`,
  `ui_palette_panel_copy_hex_and_updates_open_picker`,
  `ui_convert_to_indexed_preview_zoom_and_pan`.
- **Duplicate palette entries are indistinguishable**: pixels store color values, so two
  identical entries (say two blacks) can never be painted as separate indexes; exports and
  `remap_document_exact_color` always use the FIRST matching index. The panel flags duplicates
  in the readout with a tooltip suggesting the 1-step-nudge trick (#000000 vs #010101). This is
  inherent to constrained-RGBA; per-index painting of identical colors would require true
  indexed storage (deliberately out of scope). Built-in presets are pre-deduplicated.
- Coverage: `palette_*`, `tool_writes_snap_to_palette_when_constrained`, and
  `psd_round_trips_palette_resource` in test_main.cpp; `ui_palette_*`, `ui_convert_to_indexed_*`,
  `ui_indexed_bmp_open_adopts_palette`, and `ui_png8_export_round_trips_indexed` in
  ui_visual_tests.cpp. Preset provenance notes live in NOTICE-THIRD-PARTY.md; preset ids are
  persisted in palettes and settings, so never rename them.

## Blend modes: full Photoshop set, PDF-spec non-separable math

`BlendMode` (core/layer.hpp) is APPEND-ONLY: values ride combo-box item data and the
PSD/Aseprite maps. July 2026 added Exclusion, Hue, Color, LinearDodge ("Linear Dodge
(Add)" / Aseprite "Addition"), Subtract, and Divide, and moved the four non-separable
modes (Hue/Saturation/Color/Luminosity) from an HSL-lightness approximation to the
PDF-spec luma-based set_lum/set_sat algorithm that Photoshop and Aseprite share
(`compositor_applies_extended_blend_modes` was re-pinned deliberately). Verified against
BOTH apps on identical probe layers: Photoshop's flatten matches exactly on the separable
modes and within 1/255 on Hue/Color (scratch psd_blend_probe COM run), and
`aseprite_blend_modes_match_aseprite_render` pins Aseprite 1.3.17's own render of a
committed fixture in-suite. Rounding details that matter: Exclusion rounds the s*d/255
product BEFORE doubling (the un8 multiply both apps use) and Divide rounds to nearest.
Adding a mode means updating blend_math.cpp, blend_mode_ui.cpp (display order is
Photoshop's menu grouping, decoupled from enum order via item data), the three PSD maps
(4-char key, lfx2 stringID read AND write; Subtract/Divide are "blendSubtraction" and
"blendDivide"), and the Aseprite map in both directions.

## File formats: registry-first dispatch, one filter table, import notices

The flat/retro format support (July 2026) reshaped file I/O; new formats slot in with
one table row + one registry row + one writer branch.

- **FormatRegistry is real now**: `builtin_format_registry()` (format_registry.cpp,
  function-local static) is the single instance; `load_document_from_path`
  (main_window.cpp) consults it BEFORE the QImageReader fallback (a registry read that
  throws still falls back to Qt where a Qt plugin exists, but the REGISTRY error is
  reported when Qt fails too: it names the real problem). Handlers may be read-only
  (`write == nullptr`) and may carry a `sniff` content check (used to disambiguate `.ase`:
  Aseprite magic 0xA5E0@4 vs Adobe `ASEF` swatches: the Aseprite reader throws a message
  pointing at the Palette panel for swatch files).
- **One filter table**: `file_format_entries()` in main_window.cpp generates
  open/save/export filters, `is_supported_image_extension`, `save_file_filter_for_path`,
  and `path_with_default_extension`. Display names sit in `QT_TRANSLATE_NOOP("QObject", ...)`;
  update patchy_ja.ts when adding one.
- **Formats** (all read AND write; modules in src/formats/, Qt-free, explicit-endian via
  `binary_le.hpp` (LE) or `psd_binary.hpp` (BE)): PSD/PSB, BMP, **ICO/CUR**
  (multi-size; every embedded size imports as a hidden layer named "WxH": the writer
  reuses a matching "WxH" pixel layer verbatim, so small sizes round-trip; 256px entries
  are PNG-compressed via an injected Qt codec, `ico::set_png_codec`, installed by
  `install_ico_png_codec()` in the MainWindow ctor; CUR hotspots ride layer metadata
  `patchy.cursor_hotspot` and prefill the export dialog), **TGA** (types 1/2/3/9/10/11,
  both origin flags; 15/16-bit rejected; palette-mode docs write type 1 indexed),
  **GIF** (write-only encoder gif_document_io.cpp: reading stays with the bundled qgif —
  so the Windows package must ship `imageformats/qgif.dll`; build-release.bat's
  `CopyRequiredImageFormatPlugins` list includes it explicitly (macdeployqt and the Flatpak
  KDE runtime bundle it on the other platforms);
  LZW width-growth uses the pre-increment check, verified against Qt + Pillow, and
  `gif_encoder_bytes_are_stable` pins the exact bytes by FNV hash), **Aseprite**
  (frame 1 only; layer tree/blend modes/opacity round trip; zlib cels via vendored
  `src/formats/miniz/`; verified by driving installed Aseprite CLI), **PCX** (8-bit
  indexed EOF-palette + 24-bit 3-plane RLE), **ILBM/PBM** (ByteRun1 via the shared
  `psd::decode_packbits`/`encode_packbits_row`: the encoder was promoted from
  psd_document_io.cpp to psd_descriptor.{hpp,cpp}; EHB supported, HAM rejected, writer
  emits planar ILBM with masking type 2 for transparency). PNG/JPEG/TIFF/WebP stay on Qt.
- **Shared writer helpers** (formats/document_flatten.{hpp,cpp}): `flatten_document_rgba8`
  (masked-aware: a document-alpha layer exports non-destructively),
  `indexed_flatten_for_palette_mode` (document palette in file order, exact-then-LUT,
  appended transparent slot: the PNG-8 semantics), and `indexed_flatten_quantized`
  (median-cut fallback for RGB docs; GIF + ILBM share it).
- **Import notices**: readers report dropped/approximated features via
  `FormatReadResult::notices` (plain English, like reader error strings: the formats lib
  is Qt-free). `open_document_path` shows them in the STATUS BAR by default (first note
  plus a "+N more" suffix); the consolidated `importNoticesMessageBox` popup appears
  only when `imports/showPsdWarningsAndInfo` is enabled (the same preference that gates
  the PSD compatibility report; Seth: no info popups by default). Animated GIFs note
  "first frame only" from the Qt path. Tests that open notice-raising files assert
  `statusBar()->currentMessage()`; only tests that ENABLE the preference need the
  REPEATING QTimer dismisser (a one-shot fires during the open-progress phase and the
  suite hangs; see `ui_import_notices_dialog_shown_when_setting_enabled`).
- **Aseprite is the layered save** in Save As (routed in save_document_to_path next to
  PSD); everything else flat-exports through `write_flat_image_file`, which also applies
  `ImageSaveOptions::export_scale` (nearest-neighbor 1-8x, EXPORT flow only: the combo
  persists its own `saveOptions/exportScale` key precisely so Save/Save As option
  defaults can never pick a stale scale up; `scaled_flat_document` keeps the doc-alpha
  mask structure and palette metadata so every writer path stays faithful).
- **Fixtures + verification**: committed fixtures live under `test-fixtures/<format>/`
  (provenance in NOTICE-THIRD-PARTY.md: CPython + VS Code icons, Pillow-authored
  ICO/CUR/TGA/PCX/GIF, Aseprite-CLI-authored .aseprite); synthesized adversarial files
  are built byte-by-byte in-test. Writers were verified with independent decoders
  (Pillow, Qt, real Aseprite, a from-scratch Python ILBM reader): keep doing that for
  format changes.

## PSB (large document format) read + write

PSB support (July 2026) threads `Header::large_document` / `WriteOptions::large_document`
through psd_document_io: u64 section/layer-info/channel lengths, u32 RLE row byte counts,
header version 2, Save As offers `.psb`, and writing a >30k px document as `.psd` errors
("use .psb"; the PSB cap is 300k). Facts pinned against Photoshop 2026 (COM byte-diffs)
that the spec gets wrong or omits:

- **Tagged-block length width on read = '8B64' signature OR (PSB and the key is in the
  documented 8-byte list)** — BOTH rules, not either alone. PS writes 'cinf' as
  8B64+u64 in PSBs (not in the spec's list), but PS 2023 also writes 'lnk2' as plain
  '8BIM' + u64 (spec-list key, no 8B64 signature); honoring the signature alone
  misreads that length and silently derails the rest of the global block walk (the
  10cm-table-tent linked-smart-object regression, July 2026;
  `psb_linked_smart_objects_parse_lnke_if_available` pins it).
  `UnknownPsdBlock::long_length` records each preserved block's WIDTH for re-emit; the
  writer's upgrade list (`tagged_block_length_is_u64`) = spec set + 'cinf'.
- PS pads the PSB layer-info section to 2 bytes (same as PSD), not 4.
- The default-false PSD paths are pinned byte-identical by
  `psd_layered_writer_bytes_are_stable` (FNV hash canary; re-pin only for deliberate
  format changes).

## Smart objects (Photoshop placed layers)

Smart objects are first-class (July 2026, milestones M1 recognition/preservation, M2
edit/replace contents, M3 convert/place/via-copy/transform). A smart object stays a
**`LayerKind::Pixel` layer** (the text-layer pattern) whose pixels are Photoshop's
rendered preview; identification is the `layer_is_smart_object()` predicate over
`patchy.smart_object.*` metadata (core/smart_object.hpp owns the keys and helpers, like
adjustment_layer.hpp does).

- **Model**: per-layer placement metadata (source uuid, Trnf quad, size, dpi, lock
  reason, raster_status, block_dirty) parsed from the 'SoLd' (authoritative; 'SoLE'
  alias) or legacy 'PlLd' blocks by `src/psd/psd_smart_objects.{hpp,cpp}`; the source
  payloads live in `DocumentMetadata::smart_objects` (a `SmartObjectStore` parsed from
  the global 'lnk*' blocks, one `SmartObjectSource` per element with **shared_ptr-held
  bytes so the 40-deep whole-Document undo snapshots share one copy** — before this,
  every snapshot deep-copied embedded files). Unparseable blocks/elements stay opaque
  and re-emit verbatim.
- **Preserve-unless-edited**: untouched layers re-emit their original SoLd/PlLd blobs
  byte-identically; a move/transform sets `block_dirty` and the writer REGENERATES the
  blocks patch-in-place via the generic descriptor writer (psd_descriptor.cpp
  `write_descriptor`, which preserves key order and each id's charID/stringID form —
  Photoshop writes the 4-char key "warp" as a stringID, so id form is NOT derivable
  from length; `DescriptorObject::key_order`/`KeyEntry::long_form` carry it).
  `psd_descriptor_writer_round_trips_sold` pins read→write byte-identity.
- **Lock classification** (kLayerMetadataSmartObjectLock): warp/quiltWarp, non-affine
  quads, filterFX (Smart Filters), external ('liFE') sources, PlLd-only ("legacy"), and
  parse failures are preview-locked: Patchy shows/preserves Photoshop's raster and
  refuses pixel edits; rasterize always works. Empty lock = editable (M2+).
- **UI semantics**: "smart" badge in the layer row details; painting refused with a
  status hint; Delete removes the object (like text); merge targets and
  rasterize/rasterize-style strip the smart-object data (`strip_layer_smart_object_data`);
  Export Smart Object Contents... (layer context menu) writes the embedded bytes
  verbatim; cross-document layer paste carries sources via
  `ClipboardPayload::smart_object_sources` (uuid collision reuses the target's source,
  PS's shared-source rule).
- Opening a PSD with smart objects emits **import notices** (editable /
  preview-locked / external / dangling counts). These follow the status-bar-by-default
  rule above; UI tests opening such files through the window path only need the
  repeating dismisser when they enable `imports/showPsdWarningsAndInfo`. Tests reading
  via `DocumentIo::read_file` directly are unaffected (no dialogs).
- **Edit Contents (M2)**: double-clicking an editable embedded smart object (or the
  layer context menu's Edit Smart Object Contents) opens the source as a child tab
  titled "file.ext (embedded in Parent.psd)". The child is a full `DocumentSession`
  whose `smart_object_link{parent_session_id, source_uuid}` carries session IDS (never
  pointers: sessions_ erases on close; `session_id` is the stable identity). Save
  (Ctrl+S) on the child routes `save_document()` to
  `commit_smart_object_child_session`: the child serializes in the source's own format
  (PSB for '8BPB' via `WriteOptions::large_document`, PSD for '8BPS', Qt image formats
  re-encode a flatten), the element gets fresh shared_ptr bytes (dirty), EVERY layer
  sharing the uuid re-renders (`smart_object_render.{hpp,cpp}`:
  quadToQuad + resample_transformed_rgba8, raster_status=patchy_raster), and the
  parent takes ONE undo snapshot ("Edit Smart Object Contents"); the child marks
  clean. Sz-changing commits rescale quads per the E5 rule below; same-size commits
  leave SoLd untouched (only the lnk element regenerates). Closing a parent with open
  child tabs prompts (closeSmartObjectChildrenMessageBox) and resolves the children
  first; a child whose parent is gone detaches and falls back to Save As. Nested
  smart objects work by construction (each child is a full session; PSD children keep
  their own store through DocumentIo). Decode fidelity: PSD/PSB sources render their
  preview from the child's own flattened composite (prefer_flat_composite), so
  untouched children look exactly like Photoshop's stored pixels.
- **Edit-format guard**: PSD/PSB (DocumentIo both ways) and png/jpg/jpeg/tif/tiff/
  bmp/webp (Qt both ways) are editable; GIF and registry-only formats (TGA/PCX/...)
  decode for preview but refuse Edit/Replace with an explanatory status message
  (`classify_smart_object_contents`); undecodable/external sources keep Photoshop's
  preview forever. Content density comes from `smart_object_source_dpi` (PSD
  resolution or the image's embedded density, 72 fallback) - note Qt-authored pngs
  carry a pHYs chunk, so replacing with one honors its dpi like Photoshop does.
- **Replace Contents (M2)** follows the E5 semantics exactly: fresh uuid + element,
  every referencing layer repoints and rebuilds about its own quad center, old
  element removed, layer names swap the source stem. Accepted replace formats:
  psd/psb/png/jpg/jpeg/tif/tiff/bmp (webp only round-trips existing sources, its PS
  filetype OSType is unpinned).
- **Linked-file workflows (July 2026)**: external ('liFE') smart objects are fully
  workable. Edit Contents resolves the path (relPath against the parent folder, then
  a bare-filename sibling, then originalPath, then the fullPath URI:
  `resolve_smart_object_external_path`) and opens the REAL file as a disk-backed
  session with `SmartObjectLink::external`; its Save writes the file FIRST, then
  `refresh_external_smart_object_after_save` re-reads the bytes, stamps the liFE
  date/size, marks the element dirty, and re-renders every uuid-sharing layer as ONE
  parent undo step. Update Smart Object Content re-reads on demand; document open
  compares stored date/size and prepends "changed on disk"/"not found" notices
  (actionable notices go FIRST so the status bar shows them). Relink to File...
  follows PS's E14-captured semantics: like Replace Contents but staying external (a
  FRESH element uuid in the lnkE block, referencing layers repoint and rescale about
  their own centers per E5, layer names swap the source stem, old element pruned).
  Embed Linked follows E13: ANOTHER fresh uuid, the element moves to lnk2 as liFD,
  the emptied lnkE stays behind, locks clear, and the per-layer SoLE block key flips
  to SoLd (block_dirty repoints the Idnt on save). COM note: linked placement needs
  `putBoolean(charIDToTypeID("Lnkd"), true)` on the 'Plc ' descriptor. No
  QFileSystemWatcher by design (open-time check + explicit Update + self-save refresh
  cover the workflows). The shared re-render walk is
  `refresh_smart_object_layers_for_source`.
- **E4 acceptance (July 2026, M2)**: Photoshop 2026 opened Patchy's committed,
  replaced, and nested-edit outputs (the `ui_smart_object_*.psd` test artifacts),
  color-sampled the re-rendered previews bit-exactly, opened each embedded contents,
  and resaved cleanly (no "disk error (-1)"). Re-run the e4_acceptance COM script
  over those artifacts after any writer-side smart-object change.
- **Authoring (M3)**: Convert to Smart Object (layer menu + context submenu) moves the
  selected layer trees into a child document (canvas = union of render bounds, PSB,
  '8BPB', named "<top layer>.psb"; nested sources adopt into the child store), the
  topmost selected layer's slot/id/name become the new smart object, and the preview
  composites pixel-identically for Normal/100% layers. Place Embedded (File menu)
  uses the E2 rule dpi-aware (physical size 1:1 centered, fit-to-canvas when larger).
  Both write a from-scratch SoLd via `psd::author_placed_layer_sold_payload`, which
  mirrors PS 2026's exact key order/id forms (E1 captures;
  `smart_object_authored_sold_matches_photoshop_shape` pins the shape against the
  committed fixture) and is pushed into the layer's unknown blocks at author time, so
  the normal preserve/patch-in-place machinery owns it from then on. Patchy writes NO
  legacy PlLd companion (PS writes both; it accepts SoLd-only files, E4-gated). New
  Smart Object via Copy clones the element under a fresh uuid (E8 semantics); free
  transform composes the delta into the quad and re-renders through
  `smart_object_transform_render_callback_` (text-layer pattern), with the resampled
  pixels as fallback.
- **Warped smart objects are "unparsed"**: Photoshop stores warp meshes as 'ObAr'
  (object array) descriptor values, which psd_descriptor does NOT model, so a real
  warped SO fails SoLd parsing and imports with lock="unparsed" and an EMPTY uuid
  (badge + paint/transform/move protection; `layer_has_movable_pixels` pins the layer
  because no quad can ride a move). The dangling-uuid cleanup deliberately skips this
  state. Blocks re-emit verbatim forever (`psd_unparsed_smart_object_locks_*` pins it
  against the ps2026_e6_warp_* local fixtures). Locked-but-parsed reasons (filters /
  external / legacy / warp-without-mesh) still translate their quads on move and
  regenerate patch-in-place; nonAffineTransform moves by the per-corner DELTA (never
  overwritten with the affine quad, which would flatten a perspective placement), and
  warpNone warp bounds stay the CONTENT rect (0,0,h,w) like PS writes them.
- **Layer context menu**: reorganized into submenus (Layer Style / New / Smart
  Objects / Layer Mask + the Lock submenu) because the flat menu outgrew the screen.
  Seth's standing rule: **Edit Layer Styles... stays the FIRST item, always**
  (`ui_layer_context_menu_keeps_edit_styles_on_top` pins it). Test helpers search
  submenus recursively (`find_menu_action_by_text`).
- **Photoshop 2026 ground truth** (COM captures, July 2026): Convert-to-SO writes BOTH
  PlLd and SoLd; child doc = "<layer name>.psb" ('8BPB', creator '8BIM'), canvas = layer
  pixel bounds, Trnf = those corners; Place uses filetype 'png ' etc. with creator four
  spaces, smaller-than-canvas places 1:1 centered, larger fits-to-canvas centered
  (`generalPreferences.placeRasterSmartObject` gates SO creation — it is FALSE on this
  machine, flip it temporarily in scripts and restore); lnk2 elements are version 8
  with ~117 undocumented trailing bytes (parse by element length; Patchy writes v7);
  duplicates share the Idnt uuid, "New Smart Object via Copy" clones the element under
  a fresh uuid ('placed' is a per-layer instance uuid, always fresh); rasterizing in PS
  KEEPS the orphaned lnk2 element, so Patchy never prunes unreferenced sources either.
- **liFE (linked external file) element layout** (pinned from the 10cm-table-tent
  PS 2023 capture, v7 elements): uuid pascal string, unicode filename, filetype
  OSType, creator = four NUL bytes (not "8BIM"/spaces), u64 datasize (0), open
  descriptor flag + `{null; compInfo{compID:-1, originalCompID:-1}}`, then a SECOND
  u32 descVersion 16 + `ExternalFileLink` descriptor (class strID; keys in order:
  descVersion(str) long = 2, 'Nm  '(char) TEXT filename, fullPath(str) TEXT file://
  URI, originalPath(str) TEXT native path, relPath(str) TEXT), then a 16-byte date
  struct {u32 year, u8 month, u8 day, u8 hour, u8 minute, f64 seconds} (the mod time
  PS compares for staleness), u64 file size, then the versioned tail every element
  kind shares: v5+ unicode child doc id, v6+ f64 assetModTime, v7+ u8 lock state.
  `parse_link_element` models all of it (best-effort try/catch: unmodeled variants
  degrade to the verbatim skip) and `serialize_external_element` mirrors it for dirty
  external sources; clean elements still re-emit byte-identically.
  `psb_life_trailer_fields_parse_if_available` +
  `smart_object_external_element_round_trips_if_available` pin both directions.
- **Replace Contents ground truth** (E5 COM captures, July 2026, `ps2026_e5_*` local
  fixtures): PS creates a NEW element with a fresh uuid and repoints EVERY layer that
  referenced the old uuid (the replaced-away element is removed, unlike rasterize
  orphans); each layer's quad is rebuilt about its own quad center preserving the
  content-inch-to-doc-pixel linear map (`L_new = L_old * dpi_old/dpi_new`; same-dpi
  replaces degrade to pure pixel scaling), Sz/Rslt become the new content's pixel size
  and dpi, and layer names swap the old source stem for the new one ("A copy" becomes
  "B copy"). PS additionally rounds the resulting quad corners to whole pixels; Patchy
  keeps full doubles (a sub-pixel placement difference on OUR edit, not a render
  divergence). Place also writes an empty 'lnkE' block alongside 'lnk2'.
- Fixtures: committed `test-fixtures/psd/photoshop-place-embedded-png.psd` (real placed
  SO) + `photoshop-basic.psb`; local-only ps2026_* captures in `local-test-fixtures/psd/`
  (converted SO with 1MiB child, smart filter, via-copy, fit-placement) plus the
  eon_spider originals, and `local-test-fixtures/psd/PSBtest/` (Seth's manual end-to-end
  file: `10cm table tent.psb` embeds `Content.psb`/`Content B.psb` as smart objects, good
  for exercising PSB-in-PSB editing by hand). Coverage: `psd_smart_object_*`, `psb_*`,
  `psd_descriptor_writer_*`, `smart_object_rescaled_placement_*`, and
  `smart_object_store_*` in test_main.cpp; `ui_smart_object_*` in ui_visual_tests.cpp
  (edit-contents commit/undo, locked refusal + parent-close prompt, replace repoints
  shared layers, nested chain commit).

## Import menu, sprite sheets, seamless tile preview

- **File > Import** (`fileImportMenu`) holds "From Scanner or Camera..." (Windows-only,
  `#ifdef Q_OS_WIN`; WIA 2.0 `IWiaDevMgr2::GetImageDlg` in src/ui/scanner_import_win.cpp,
  WIN32-gated in CMakeLists + `wiaguid` lib) and "Sprite Sheet to Layers...". Scanner
  imports land as an untitled+modified "Scanned Image" session with DPI clamped to
  10-4800 (else 300); `PATCHY_FAKE_SCANNER_FILE=<path>` bypasses COM so offscreen tests
  can exercise the plumbing (`ui_scanner_import_creates_untitled_document`); the real
  acquire path is manual-verify with a device.
- **Sprite sheets** (src/ui/sprite_sheet_dialog.*): `compose_sprite_sheet` renders one
  frame per visible top-level layer (bottom-to-top, each isolated in a scratch document
  so blend/opacity/styles render against an empty backdrop) into a padded grid;
  `slice_sprite_sheet` cuts a cell grid into hidden "Frame N" layers (first visible),
  skipping fully-transparent cells. MainWindow's export/import methods just wrap dialogs
  + the normal export machinery around these two testable functions.
- **View > Seamless Tile Preview** (src/ui/tile_preview_window.*): a Qt::Tool window
  painting the composite tiled across the viewport, NOT in-canvas (the canvas dirty-rect
  sites would each need 9-way replication). A 150 ms timer polls a const-only revision
  probe (mutable Layer accessors bump revisions: probe through const refs); docs over
  1 Mpx switch to the Refresh button. Drag pans the wrapped tiling (double-click
  recenters; `panOffset` dynamic property for tests) and a `VisibleSizeGrip` (promoted
  from brush_tip_picker.cpp to dialog_utils; frameless chrome has no native resize
  border) resizes it, size remembered via `ui/tilePreviewWindowSize`. Closing the window
  unchecks `viewTilePreviewAction` via a `done(int)` override. GOTCHA for any chrome
  dialog that reacts to closing: `done()` is the only correct funnel. `reject()` (chrome
  X, Esc) hides WITHOUT a QCloseEvent, so closeEvent-based cleanup misses it; but
  overriding `reject()` to call `close()` is worse, since `QDialog::closeEvent` itself
  calls `reject()` and treats a dialog still visible afterwards as a vetoed close,
  turning EVERY close path into a no-op. `MainWindow::closeEvent` closes an open
  preview during shutdown (a surviving one has no visible transient parent, blocks
  lastWindowClosed, and leaves a headless process). All three failure modes are pinned
  by `ui_tile_preview_window_tracks_document_edits`.

## Merge Down flattens folders and any multi-selection

`MainWindow::merge_down()` ("Merge Down" / Ctrl+E) flattens a selection into one pixel layer,
mirroring Photoshop. A single **folder** flattens in place ("Merge Group"); a single non-folder
merges with the layer directly below it; two or more selected items (folders and/or layers, any
folder, contiguous or not) merge together. The merged pixels land in the **bottom-most visible**
item, which keeps its id and name; a non-pixel target (folder/adjustment/text) is replaced by a
fresh pixel layer. So selecting everything (including the folders) collapses to a single flat
layer with no folders left.

- **Hidden layers are discarded, not blocking.** A selected layer with visibility off contributes
  nothing and is deleted by the merge (Photoshop's "delete hidden layers" behavior) — it must never
  abort the operation. Likewise a folder flattens only its *visible* children.
- Folders/adjustment layers are added to the scratch merge `Document` as-is and the compositor
  flattens/applies them; group opacity/blend/mask follow whatever the canvas shows (currently
  pass-through). Anything the compositor can't draw is skipped the same way as a hidden layer.
- A cross-folder merge of **leaf** layers (folders themselves not selected) can leave an emptied
  folder behind — that is intended, not a bug.
- Coverage: the `ui_merge_down_*` tests in `ui_visual_tests.cpp`.

## Delete key on a text layer deletes the object, never its pixels

`MainWindow::clear_active_layer()` ("layer.clear", default shortcut Delete, also Layer > Clear
Layer / Selection) special-cases text layers (pixel layers carrying text metadata,
`layer_is_text`), matching Photoshop. Pixel-clearing a text layer is forbidden because it leaves
an invisible layer whose metadata resurrects the "erased" text on the next text-tool click.
Instead: with no marquee selection and no inline text edit in progress, targeted text layers are
deleted outright (a pure-text selection delegates to `delete_layers`, undo label "Delete layer");
with a selection active they are skipped with a status-bar hint; while the `inlineTextEditor` is
alive they are left completely alone (Delete belongs to typing). A mixed selection clears the
pixel layers and deletes the text layers in one undo step. Coverage:
`ui_delete_key_action_removes_text_layer_object` in `ui_visual_tests.cpp`.

## Bitmap brush tips (Photoshop-style) and .abr import

The Brush and Eraser tools can stamp bitmap **brush tips** in addition to the procedural
round/soft brush. Key pieces:

- Core stamping lives in `src/core/brush_tip.hpp/.cpp` (`BrushTip` = 8-bit grayscale coverage
  mask + default spacing; `BrushTipMipChain` box-filtered halvings built once per tip;
  `ScaledBrushTip` resampled per brush size) and `src/core/pixel_tools.cpp` (`paint_tip_dab`,
  `paint_tip_segment`). `EditOptions::brush_tip` (non-owning `const ScaledBrushTip*`, null =
  procedural path) plus `brush_tip_spacing` select the stamp path. Rotation/roundness/subpixel
  are applied per-dab in the inverse map, so the scaled cache depends only on size (pressure
  size is quantized to 2px steps while a tip is active; CanvasWidget keeps an 8-entry LRU of
  scaled stamps). The stateful `paint_brush_segment(..., BrushTipStrokeState&)` overload carries
  dab spacing across the short segments the stroke smoother emits — the state resets in
  `CanvasWidget::clear_brush_stroke_tracking()`.
- `.abr` parsing is `src/psd/abr_reader.*` (`read_abr`): supports v1/v2 and v6/v7/v10
  (subversion 1 = 47-byte, 2 = 301-byte samp entry key skip), pairs `samp` bitmaps with the
  `desc` ActionDescriptor brush list **in order** for names/spacing, decodes per-row PackBits
  RLE, downconverts 16-bit, crops to content, caps dimensions at 4096, and skips bad entries
  with warnings instead of failing the file. v6 8BIM tagged blocks are padded to 4-byte
  boundaries with the padding excluded from the length field — the block walk must skip the
  padding or the next signature read lands mid-pad (latent until July 2026; only surfaces on
  files whose desc length is not already 4-aligned). The descriptor parser + `decode_packbits`
  were extracted from psd_document_io.cpp into the shared `src/psd/psd_descriptor.*` — PSD and
  ABR both use them.
- The user library is `src/ui/brush_tip_library.*`: `<settings dir>/brushes/` (i.e.
  `%APPDATA%/Patchy/brushes/`) with one grayscale PNG (coverage mask) + one JSON sidecar
  (`{"name", "spacing", "folder"}`) per tip; ids are the UUID filename stems. Corrupt files skip
  that tip only. Tests point the library at a temp dir via the constructor arg. Tips carry an
  organizational **folder** (empty = ungrouped, sorted first); `import_abr` files everything
  into a folder named after the .abr; `remove_tips()` bulk-deletes with a single `changed()`.
- **Built-in default tips** (`src/ui/default_brush_tips.*`): 36 parametrically generated tips —
  the 16 natural-media originals (chalk, charcoal, pastel, bristle, sponge, canvas, smoke,
  spray, spatter, stipple, ink splat, grunge, square, calligraphy, star, grass) plus the 20
  v3 stamps (dotted/dashed line, stitches, chain, rope, arrow, brick road, cobblestone, leaf,
  snowflake, rain, bubbles, flower, sparkle, heart, confetti, paw prints, footprints,
  crosshatch, RTsoft logo) — all deterministic seeded noise/SDF math, no binary assets.
  `MainWindow::brush_tip_library()` seeds them into the "Patchy Defaults" folder gated by the
  `brushes/defaultTipsVersion` settings int. Each spec carries `since_version`, and the
  startup gate calls `restore_default_tips(stored_version)` so a version bump seeds ONLY tips
  newer than the install — a user deleting older defaults is respected (the parameterless
  overload, used by the manager's "Restore Default Brushes" button, still restores everything
  missing). Direction-control stamps author their art with bitmap +X = direction of travel
  (a rightward stroke stamps unrotated). Tiling stamps (brick road, cobblestone, rope) are
  square/X-periodic with coverage touching the tile edges, because `add_tip` crops to content
  and dab advance = brush size x spacing (spacing 1.0 = exact butt joint). UI tests suppress
  seeding via `clear_brush_tip_test_state()` setting the version high; the contact-sheet test
  `ui_default_brush_tips_seed_once_and_render_sheet` renders every tip for visual review
  (artifact `ui_default_brush_tips_sheet.png`).
- UI: `BrushTipPicker` (options-bar popup grid + folder filter combo, Brush+Eraser only) and
  `request_brush_tip_manager` (`src/ui/brush_tip_manager_dialog.*`): QTreeWidget grouped by
  expandable folder rows, Extended multi-selection (Shift ranges), bulk delete via button or
  Del key (a selected folder row stands for all its tips), folder line-edit applies to the
  whole selection, live stroke preview painted by the real engine. "Define Brush Tip from
  Selection" (Edit menu, hotkey id `edit.define_brush_tip`) captures inverted luminance ×
  alpha × selection coverage — dark pixels paint, Photoshop semantics. The active tip is
  application-wide (re-applied in `apply_active_brush_settings_to_canvas`) but deliberately
  NOT persisted: every launch resets the brush to the "Round" startup preset (round tip,
  size 25, 100% opacity, 0% soft; `default_startup_brush_preset_id` applied in
  `load_tool_settings`), because restoring a stale tip or a barely-visible opacity confuses
  users. Eraser opacity/softness reset the same way; only `tools/eraserSize` survives a
  restart, and the old `tools/brushTip`/`brushSize`/`brushOpacity`/`brushSoftness`/
  `brushBuildUp`/`brushPreset`/`eraserOpacity`/`eraserSoftness` keys are neither read nor
  written anymore.
- Dialog spin boxes that keep their - / + buttons must append
  `dialog_spinbox_button_style()` (src/ui/dialog_utils) to the dialog stylesheet AFTER all
  children exist — see the sub-control gotcha note in that header (Preferences and the Brush
  Tips manager both use it).
- The Brush/Eraser cursor traces the active tip's actual outline
  (`CanvasWidget::apply_brush_tip_cursor`); when the on-screen footprint exceeds the ~155px OS
  cursor cap (tip or round), the cursor becomes a crosshair and the outline is drawn as a canvas
  overlay following the pointer (`brush_outline_uses_overlay`/`draw_brush_hover_outline`, hover
  tracked in mouseMoveEvent, cached outline image). The Alt+drag size overlay previews the
  red-tinted stamp footprint. Base shape only — per-dab pen rotation/tilt are not reflected.
  When the size changes while the pointer is stationary (the `[`/`]` hotkeys → `set_brush_size`/
  `set_quick_select_size`), invalidate the **previous** outline rect, not just the new one:
  `update_tool_cursor()` only repaints the new (possibly smaller) rect, so shrinking a large
  brush would strand the old larger rings on the canvas. `invalidate_brush_hover_outline(previous)`
  erases `old ∪ new`. This can't be caught by a `grab()` pixel test (grab repaints the whole
  widget), so it is a manual-verify behavior.
- Holding **Alt** over a paint/shape/fill tool (`tool_uses_alt_left_for_color_pick`) temporarily
  turns a left-click into a colour pick; `update_tool_cursor` shows a drawn eyedropper cursor for
  it (and for the standalone Eyedropper tool), hotspot on the lower-left sampling tip, checked
  before the brush/overlay branches so it wins at any size. The `eventFilter` swaps the cursor the
  instant Alt toggles, driving it from the event's folded modifiers via the transient
  `alt_color_pick_cursor_override_` — not `QApplication::keyboardModifiers()`, which can lag the
  app-level filter and otherwise leave the eyedropper stuck on release. Same reason the
  Zoom/selection modifier badges use folded modifiers, not the live state.
- The eyedropper cursor glyph is a shared helper, `patchy::ui::eyedropper_cursor()` in
  `ui/tool_cursors.{hpp,cpp}` (built once, cached). Used by both the canvas Alt/Eyedropper cursor
  and the colour picker's "Pick Screen Color" full-screen overlay (`ScreenColorOverlay` in
  color_panel.cpp) so the two never drift — edit the glyph in that one place.
- The **Soft** setting applies to bitmap tips as an outward edge feather:
  `patchy::soften_scaled_brush_tip` (3× separable box blur, pads the stamp and shifts the
  anchor), driven by `CanvasWidget::scaled_brush_tip_for(size, softness)` whose cache is keyed
  by (size, feather). Feather = size × softness% / 400. Note a soft tip's center coverage can
  drop below 100% for thin tips — soft erase leaving residue is correct behavior.
- All brush/eraser strokes — procedural AND bitmap-tip — share the per-stroke snapshot
  compositor (`install_brush_stroke_compositor` + `write_brush_stroke_pixel_from_snapshot`):
  overlapping dabs accumulate alpha toward the stroke's opacity cap and re-blend against a
  stroke-start layer snapshot, so soft overlaps (self-crossing strokes, edge-to-edge stamps
  like brick) never leave light seams. Tips route through it via
  `EditOptions::stroke_pixel_writer`, honored in `paint_tip_dab`
  (`ui_brush_tip_soft_stamps_accumulate_without_seams` pins the no-seam behavior). Only the
  layer-mask brush and Clone still use the older per-stroke max-coverage cap
  (`capped_stroke_coverage`), called directly rather than via EditOptions.
- Brush size maxes at **1024** (canvas clamps, Alt+drag clamps, and the options-bar
  spin/slider/hotkeys all use `kMaxBrushSize`; Quick Select also has its own 512px cap).
- The `[`/`]` and Shift+`[`/`]` hotkeys (and the pen-button Increase/Decrease brush
  size actions) resize **proportionally**, Photoshop-style: `proportional_brush_step`
  (anon namespace in main_window.cpp) grows the size by 10% (Shift = 30%) with a 1-px
  floor (2 px for Shift), so big brushes resize fast while small ones keep 1-px
  precision. Growing scales by `(1+f)` and shrinking by `1/(1+f)` so `]` then `[`
  returns to the same size. Don't revert to a fixed ±1/±10 step — that made large
  brushes crawl. `ui_photoshop_shortcuts_are_registered` pins the exact steps
  (20→22/26, 100→110/130, 5→6); retune the factors and that test together.
- Deleted default tips are recoverable: `BrushTipLibrary::restore_default_tips()` re-adds
  missing ones (matched by name within the defaults folder); the manager's "Restore Default
  Brushes" button pairs it with `reset_default_tips_to_factory()`, which also snaps existing
  default-folder tips whose spacing/tip shape/dynamics were customized back to the shipped
  spec AND rewrites a tip's mask PNG in place (same id) when its pixels differ from the
  current generator output — so a default seeded before a generator artwork fix heals via the
  button (only the button does this — version-gated startup seeding never resets
  customizations or masks). First-run seeding reuses `restore_default_tips` under the
  `brushes/defaultTipsVersion` gate.
- **Brush dynamics** (July 2026, Photoshop-compatible): per-dab Shape Dynamics (size/angle/
  roundness jitter with minimum floors, flip X/Y jitter), Scattering (scatter %, both axes,
  count 1-16, count jitter), Transfer (opacity jitter + Minimum Opacity), plus the static Tip
  Shape (base angle/roundness). **Every dynamic carries a Photoshop-style Control** (Off /
  Fade with per-dynamic fade steps / Pen Pressure / Pen Tilt / Pen Rotation / Stylus Wheel =
  tablet tangential pressure; the angle additionally offers Initial Direction / Direction).
  Size/roundness/opacity controls are tri-state against the global pen prefs: the default
  `GlobalDefault` leaves `input/pen/*` authoritative (pre-dab scaling in
  `effective_brush_input`); ANY other value (including Off) makes the brush own that aspect
  and suppresses the matching global modulation (size↔pressureSize, opacity↔pressureOpacity,
  roundness↔tiltShape's tilt→roundness) — gated by `brush_dynamics_authoritative`
  (Brush tool only, never the eraser end/tool, Clone/Smudge, or mask painting). Off therefore
  means "this brush ignores the pen for this aspect" and does no per-dab work: `active()`
  treats Off/GlobalDefault as inactive, so an Off-only Round brush stays on the procedural
  capsule path (the options-bar button badge keys on `brush_dynamics_is_default`, NOT
  `active()`). Control values map the input c∈0..1 into [minimum, 1] (count fades toward 1,
  scatter scales by c, angle maps c to 0..360°); a missing pen input reads as 1.0 so a mouse
  paints at full value (PS behavior). Controls never consume RNG draws — the draw-order
  contract gates stay keyed on static config (`tool_brush_tip_controls_at_full_value_change_nothing`
  pins this). Only APPEND to `BrushDynamicControl`: combo indices and casts depend on the
  existing order (`StylusWheel` and `GlobalDefault` were appended in July 2026).
  - Core engine is `src/core/brush_dynamics.hpp/.cpp` (`BrushDynamics` carried by value in
    `EditOptions`; default-constructed = `active()==false` = the historical stamp path
    bit-for-bit, zero RNG draws). Per-dab variation rides the existing inverse map
    (`TipDabTransform` gained inverse_scale + flip signs); size jitter only SHRINKS (Photoshop
    semantics), so the scaled-tip LRU cache and 2px pressure quantization are untouched. The
    RNG is splitmix64 with explicit uniform mapping — NEVER std::uniform_*_distribution
    (implementation-defined output would break cross-toolchain pixel tests). The draw-order
    contract is documented in brush_dynamics.hpp; tests depend on it. Stroke state (RNG, fade
    step index, smoothed + initial direction) lives in `BrushTipStrokeState`, seeded from
    `EditOptions::brush_dynamics.seed` on the stroke's first dab; CanvasWidget seeds per
    stroke in `clear_brush_stroke_tracking()` (`set_brush_dynamics_test_seed` is the UI-test
    hook for reproducible stroke artifacts).
  - Dynamics are Brush-tool only: erase strokes strip `brush_dynamics` in draw_brush_segment /
    draw_brush_at (flip that gate to enable eraser dynamics later). The procedural **Round
    brush supports dynamics too** (July 2026): its capsule renderer has no dab loop, so while
    `brush_dynamics_.active()` a Round Brush stroke stamps through a synthesized 256px disc
    tip (`round_dynamics_tip_mips()` in canvas_widget.cpp, spacing 25%, Soft feathers it like
    any bitmap tip, pressure sizes quantize to 2px like tips); inactive dynamics keep the
    historical capsule path bit-for-bit, and a Round ERASE stroke always stays procedural
    (the erase gate also nulls the synthesized tip). Round dynamics are **session-only**:
    they live in `MainWindow::round_brush_dynamics_` (+ base shape), are never persisted, and
    reset to plain on every launch so a weird leftover setup cannot confuse anyone.
    `draw_brush_at` routes tip presses through a stateful zero-length `paint_brush_segment`
    so the press dab is not re-stamped by the first move segment (invisible for static
    stamps, visibly double-jittered with dynamics).
  - Per-tip persistence: `BrushTipEntry` carries dynamics + base angle/roundness; the JSON
    sidecar gains top-level `"baseAngle"`/`"baseRoundness"` and a `"dynamics"` object
    (camelCase fractions 0..1, scatter 0..10, string enum tokens like `"direction"`), written
    only when non-default. The controls persist as `sizeControl`/`roundnessControl`/
    `scatterControl`/`countControl`/`opacityControl` + `*FadeSteps` + `minimumOpacity`
    (tokens `"global"`/`"off"`/`"fade"`/`"penPressure"`/`"penTilt"`/`"penRotation"`/
    `"stylusWheel"`); missing/unknown tokens read as the slot's default (GlobalDefault for
    size/roundness/opacity, Off elsewhere) so legacy sidecars need no migration, and
    angle-only tokens degrade via `sanitize_non_angle_control`. Adding a dynamics field means
    updating BOTH equality functions: `brush_dynamics_is_default` AND `persisted_dynamics_equal`
    (feeds "Restore Default Brushes") — missing one silently breaks sidecar writes or factory
    reset. An older build editing such a tip rewrites the sidecar without the
    new keys and silently drops them — accepted. The options-bar **Dynamics** button
    (`BrushDynamicsButton`, src/ui/brush_dynamics_popup.*, Brush tool only) edits the active
    bitmap tip (persisted via `BrushTipLibrary::set_tip_dynamics`, debounced ~200ms) or the
    Round brush's session values (`set_round_session`, routed on the builtin round id in the
    MainWindow `dynamics_edited` handler, never persisted); the canvas gets the values
    through `apply_brush_tip_to_canvas`. Launch behavior is unchanged: the session brush
    still resets to Round with plain dynamics; per-tip dynamics live only in sidecars. The
    manager dialog's stroke preview renders dynamics with a fixed seed.
  - ABR import extracts the supported dynamics (`parse_brush_dynamics`, abr_reader.cpp) from
    the brushPreset descriptor: `useTipDynamics` gates `szVr`/`angleDynamics`/
    `roundnessDynamics` (class `brVr`: `bVTy`/`fStp`/`jitter`/`Mnm `) + sibling
    `minimumDiameter`/`minimumRoundness` + preset-level `flipX`/`flipY` (the flip JITTERS —
    the static tip flips are the ones inside the `Brsh` object); `useScatter` gates
    `scatterDynamics`/`bothAxes`/`Cnt ` (a double)/`countDynamics`; `usePaintDynamics` gates
    `opVr` (whose `Mnm ` is the Transfer Minimum Opacity). Base `Angl`/`Rndn` come from the
    `Brsh` tip object. `bVTy` mapping: 0 Off, 1 Fade, 2 Pen Pressure, 3 Pen Tilt, 4 Stylus
    Wheel, 5 Rotation, 6 Initial Direction, 7 Direction. Every dynamic's control + fStp now
    imports; on NON-angle dynamics bVTy 0 maps to the slot default (GlobalDefault for size/
    roundness/opacity — imported jitter-only packs keep responding to the user's global pen
    prefs, which are the analog of PS's options-bar pressure-override buttons; Off for
    scatter/count) and the angle-only 6/7 degrade to Off (`non_angle_control_from_bvty`). A
    preset with texture/dual brush/color dynamics enabled imports without them plus one
    per-brush warning.
  - When `input/pen/tiltShape` is on and the tip's angle control is PenTilt/PenRotation, the
    per-dab path owns the angle and effective_brush_input skips its tilt-angle assignment
    (tilt→roundness still applies unless the roundness control overrides it) so the stamp is
    not rotated twice. Known divergence (pre-existing for jitter): dab spacing tracks the base
    brush size, not the per-dab controlled size (PS ties spacing to the dynamic size).
  - The built-in default tips ship with **curated dynamics** (`DefaultBrushTipSpec::dynamics`
    in default_brush_tips.cpp): particle/scatter tips scatter (spray/spatter/stipple/star/
    smoke/ink splat/leaf/snowflake/rain/bubbles/flower/sparkle/heart/confetti), media tips get
    subtle angle/opacity jitter, and the path stamps (dashed line, stitches, chain, rope,
    arrow, brick road, cobblestone, paw prints, footprints) plus Bristle + Grass use the
    Direction angle control (the bristle dot-row must stay perpendicular to travel). Canvas,
    Square, Calligraphy, Dotted Line, and RTsoft Logo deliberately stay plain. Seeding is
    gated by `brushes/defaultTipsVersion` = **3** (v3 added the 20 stamp tips); upgrades from
    < 2 additionally run `BrushTipLibrary::apply_default_tip_dynamics()` once — it must NEVER
    re-run for stored version >= 2 because it cannot distinguish "user reset dynamics after
    v2" from "never migrated", so a re-run would stomp deliberate resets. It only touches
    default-folder tips whose dynamics/tip shape are still untouched defaults — user
    customizations always win. The manager's "Restore Default Brushes" button re-adds missing
    tips with their curated dynamics but never re-migrates existing ones.
  - The dynamics FORM is the shared `BrushDynamicsPanel` (brush_dynamics_popup.*): the
    options-bar button popup embeds it, and the Brush Tips manager's "Edit Dynamics…" button
    (`brushTipManagerDynamicsButton`, single selection only) opens it in a child dialog
    (`brushTipManagerDynamicsDialog`) with live debounced apply + stroke-preview refresh.
    Control combos (`dynamicsSizeControlCombo` etc. + `dynamics*FadeStepsSpin`, and
    `dynamicsMinimumOpacitySpin`) map items via `addItem(text, int(enum))`/`currentData` so
    display order is decoupled from enum values — EXCEPT the pre-existing angle combo, whose
    item indices equal the enum values (a UI test sets index 6 = Direction; append-only).
    Fade-steps spins show only while their combo says Fade; Minimum Opacity is enabled only
    while the opacity control has a real source. The options-bar popup wraps the panel in a
    QScrollArea but must size itself from the PANEL's sizeHint, not adjustSize() —
    QScrollArea::sizeHint is capped at ~24 font-heights, which otherwise shows a needless
    scrollbar on any normal screen. It clamps to the screen's available height only when the
    panel truly cannot fit, widening by the scrollbar in that case.
    Tips carrying dynamics (or a non-default tip shape) show a small blue corner badge on
    their thumbnails everywhere (`brush_tip_entry_has_dynamics` /
    `brush_tip_thumbnail_with_badge` in brush_tip_library) plus a " • dynamics" tooltip
    suffix.
  - The tip picker popup is resizable via an embedded QSizeGrip (a frameless Qt::Popup has no
    native resize border); the size persists as `ui/brushTipPickerPopupSize` (saved in the
    popup subclass's closeEvent — outside-click dismissal goes through close()). The grip is
    custom-painted (`VisibleSizeGrip`, three light diagonal strokes): the native style's grip
    is nearly invisible on the dark QSS theme.
  - Patents: shape dynamics/scattering shipped in Photoshop 7 (2002); the covering patents are
    expired and GIMP/Krita have shipped equivalent jitter/scatter engines for two decades — no
    design constraint here (unlike Quick Select below).
- v1 limitations (deliberate): layer-**mask** painting and Clone/Smudge stay procedural;
  texture, dual brush, color dynamics, wet edges, and flow are neither imported nor modeled.
- Fixtures: `test-fixtures/abr/myer-settlement-brushes.abr` (CC0, see NOTICE.txt; 148 brushes,
  v6.2, dynamics all disabled — pins the defaults path) and the self-authored
  `photoshop-dynamics.abr` / `photoshop-dual-brush.abr` (exported from Photoshop 2026, one
  probe brush each with known dynamics values; the dual one exercises the unsupported-feature
  warning). The dynamics fixture's non-angle `bVTy` values are all 0 (angle = 7 Direction), so
  it pins the bVTy-0→GlobalDefault/Off mapping only; explicit control imports (bVTy 1-5,
  per-dynamic fStp, opVr Mnm, Direction-on-size→Off) are pinned by the synthesized v6 file in
  `abr_v6_desc_controls_import` (test_main.cpp writes the desc TLVs itself). Bigger CC0 sets
  for manual testing live in `local-test-fixtures/abr-sets/`.
  Coverage: `brush_tip_*`/`tool_brush_tip_*`/`brush_dynamics_*`/`abr_*` in test_main.cpp,
  `ui_brush_tip_*`/`ui_brush_dynamics_*` in ui_visual_tests.cpp.

## Tool palette icons are hand-authored SVG resources

The 20 tool icons are original SVGs at `src/ui/icons/tool-*.svg` (32x32 viewBox, `#dce2eb`
strokes ~2.4 primary / 2.0 detail, round caps/joins, at most one `#74c0ff` accent element that
is never load-bearing: icons must read from the gray geometry alone on the `#2f75bd` checked
background). Registered in `src/ui/icons.qrc`, mapped by `tool_icon()` (main_window.cpp), which
calls `qInitResources_icons()` itself (resources live in the static `patchy_ui` lib). The flyout
corner triangle is `tool-flyout-corner.svg` via the `::menu-indicator` QSS rules for the three
flyout buttons. Keep to plain SVG elements/presentation attributes (QtSvg renders a Tiny-1.2
subset; `linearGradient` works and is used by tool-gradient.svg, pinned by the visual test).
Review the whole set with `patchy_ui_visual_tests.exe ui_tool_palette_icons`, which writes
`test-artifacts/ui_tool_palette_icons_sheet.png` (normal/hover/checked/disabled at 20px + 40px)
and `ui_tool_palette.png` (real toolbar grab); the test CHECKs per-icon pixel coverage (a
typo'd qrc alias renders EMPTY silently) and pairwise distinctness. All path data is original
(generic metaphors, no Photoshop geometry), so the license stays clean.

## QListWidget rows built with setItemWidget must paint their own selection

The layers panel (`restyle_layer_rows`, main_window.cpp) and the Layer Style dialog's category
list (`restyle_category_rows`, layer_style_dialog.cpp) place opaque row widgets over their
QListWidget items, so the `::item:selected` QSS background is hidden behind them: selection
styling is applied to each row widget's stylesheet whenever the selection changes. Two gotchas
pinned down in July 2026:

- QSS `::item` padding insets the item-widget geometry, and a widget `minimumHeight` taller
  than the padded content rect wins over the requested geometry, so the widget bleeds into the
  next row (misaligned separators and selection slivers). Give such lists `padding: 0` on
  `::item` and let the row widget's layout margins provide the spacing.
- Once a row widget gets a stylesheet, its QLabel/QCheckBox children are drawn through the QSS
  path and fill with the inherited palette background; set `background: transparent` on them
  explicitly in the row's stylesheet.
- A stylesheet-styled QCheckBox needs a NON-native border in some matching rule — the app
  stylesheet's global `QCheckBox { border: none; }` covers this; do not remove it. Qt only
  suppresses QMacStyle's Aqua layout-item margins (checkboxes: +2,+3,-9,-4) for styled widgets
  whose rule has a non-native border (qstylesheetstyle.cpp, SE_*LayoutItem). With the margins
  active, box layouts deliberately overlap the neighboring label ~9px into the checkbox — right
  for the inset native glyph, but on the flat 12px stylesheet indicator the label lands ON the
  box (the July 2026 0.13-mac "text jammed into the checkbox" Layer Style bug). Only reproducible
  in the real app: the test harness never loads the QMacStyle plugin, so offscreen/test runs
  cannot catch a regression here.

## Gradient stop editor widget (shared, two-track)

`GradientStopsEditorWidget` (src/ui/gradient_stops_editor.*) is the draggable gradient-stops bar
used by both the gradient tool's "Edit Gradient Stops" dialog (single-track mode: combined RGBA
stops below the bar) and the Layer Style dialog's Gradient Overlay page
(`set_opacity_track_enabled(true)`: Photoshop-style opacity tags above the bar, RGB color tags
below, fixed height 96 instead of 66). Conventions that matter when touching it:

- The widget never mutates its own stop vectors; every interaction fires a callback and the host
  pushes the new state back through the setters. At most one stop is selected across both tracks
  (selecting on one track clears the other).
- The Gradient Overlay page's `gradient_editor_color_stops`/`gradient_editor_alpha_stops` vectors
  ARE the page's widget state: `save_controls_to_style` copies and sorts the *copies*. Never sort
  the working vectors in place — an in-flight tag drag holds an index into them.
- Single-track geometry is pinned by `ui_options_bar_tracks_active_tool` (bar y=16, tags y≥44);
  two-track geometry by the `ui_layer_style_gradient_*` and `ui_gradient_stops_editor_two_track_*`
  tests (opacity area y 0..27, bar y 30..60, color tags ~y 66..89).

## Non-modal dialogs: child dialogs close with their parent

`run_non_modal_dialog` (dialog_utils) runs QDialogs non-modally in a nested event loop, so a parent
dialog stays clickable while a child dialog (e.g. a layer-style color picker) is open. Rules pinned
July 2026, fixing the "color picker never comes up again" bug:

- If a dialog's parent window is another QDialog, `run_non_modal_dialog` auto-rejects it when the
  parent finishes. Without this, closing the parent orphans the child: the child falls behind the
  main window on the next click (a hidden owner stops anchoring it in the z-order) while its nested
  loop keeps running.
- `request_patchy_color` allows one picker at a time (static QPointer): a request while one is open
  raises the existing picker and returns nullopt; it never stacks a second picker and never
  silently no-ops without a visible trace.
- Transient pickers keep a position-memory group (`set_dialog_position_memory_id`) separate from
  the persistent Foreground/Background/Text color panel, which shares their `patchyColorDialog`
  objectName; otherwise a picker opens wherever the panel was last dragged, possibly on another
  monitor.
- On macOS, `run_non_modal_dialog` additionally anchors the dialog as a native child window of
  its parent widget's window (`keep_dialog_above_parent_window`, dialog_utils_mac.mm): macOS has
  no Win32 owned-window z-order, so clicking the edit-locked main window buried the dialog behind
  it and the app looked frozen (July 2026 0.13 mac bug). The anchor attaches on Show (deferred one
  event-loop turn) and MUST detach on Hide/Close — AppKit re-orders attached children with their
  parent even when hidden. Child windows follow parent moves; that is accepted mac-native behavior.
  Any new non-modal dialog path that bypasses `run_non_modal_dialog` needs the same call.

## Options toolbar controls share one fixed row height

Every control in the Options bar (`QToolBar#Options`) is pinned by the app stylesheet to 26px
total height (24px QSS min/max-height + 1px borders for labels, combos, spinboxes, checkboxes).
Any taller control grows the whole toolbar only while its tool is active, so the canvas shifts
up/down on tool switches. Gotcha: QStyleSheetStyle inflates QToolButton size hints by +3px (a
"broken QToolButton" workaround inside Qt), so a QToolButton placed in the bar needs an explicit
QSS min/max-height cap; icon-only buttons use the `optionsBarButton` property rule, and the
Brush/Eraser Tip picker has its own `QToolButton#brushTipPicker` rule (20px content + 2px padding
+ 1px border = 26px). `ui_brush_tip_picker_keeps_options_bar_height` asserts the bar keeps one
height across all tools.

## Options bar tool state must be mirrored and applied per document session

Each document tab owns its own `CanvasWidget`, so an options-bar control that only calls a setter
on the current canvas inside its change-signal lambda silently desyncs from any document created
or activated later (the control still shows the old state while the new canvas uses its default;
this was the shape Fill checkbox bug). Application-wide options must follow the `current_*`
mirror pattern in `MainWindow` (for example `current_marquee_style_`, `current_fill_shapes_`):
record the value in the member inside the control's signal, then apply it at both per-session
sites in `main_window.cpp` (the new-session setup block in the document-open path and
`activate_document_tab`). If the value is also loaded from `QSettings` behind a `QSignalBlocker`
in `load_tool_settings()`, update the mirror there by hand. Regression coverage:
`ui_shape_fill_and_corner_radius_apply_to_new_documents`.

## Status bar hosts the zoom percentage box via a QStatusBar subclass

The main window's status bar is a `ZoomStatusBar` (`src/ui/zoom_status_bar.*`, installed with
`setStatusBar` before any `statusBar()` call). Reason: QStatusBar hides every non-permanent
widget whenever a message is showing, and Patchy keeps a persistent message up almost always,
so the Photoshop-style zoom box (`ZoomPercentEdit`, object name `statusZoomEdit`) is a manually
positioned child at the far left, outside the item layout, and the subclass repaints the
temporary message offset to the widget's right. Any future left-side status-bar widget must use
this same pattern, not `addWidget()`. The box commits on Enter/focus-out, reverts on Escape,
zooms about the viewport center, and is refreshed from `refresh_document_info()` (guarded by
`isModified()` so it never stomps in-progress typing). Coverage:
`ui_status_bar_zoom_percent_box_edits_zoom`.

## Argument evaluation order: never read a variable beside std::move(it)

`f(g(x), std::move(x))` is broken: C++ argument evaluation order is unspecified and MSVC goes
right-to-left, so `x` is moved into the by-value parameter before `g(x)` reads it. This silently
committed empty feathered selections (marquee/lasso/wand Add deleted the whole selection, fixed
July 2026). Compute `g(x)` into a local first, or use an overload that derives the value
internally — `CanvasWidget::combine_selection_from_mask(bounds, mask)` exists for exactly this.

## Quick Select tool: solve-on-release is a patent constraint, not a UX choice

The Quick Select tool (`CanvasTool::QuickSelect`, Shift+W, flyout shared with the Magic Wand)
segments each brush stroke ONCE on mouse-release: the drag only accumulates a seed footprint
(`stamp_quick_select_segment`) and draws a translucent capsule overlay, then
`finish_quick_select_stroke` runs the cut and commits one undo entry. **Do not add live
per-mouse-move classification or selection preview before Nov 3, 2029**: Adobe's US 8050498
("Live coherent image selection", term-adjusted) claims classify-and-display while brush input
is being received. Related design rules, all deliberate: Enhance Edge is purely geometric
majority smoothing (US 8013870 claims local-color-model edge opacity until 2028), the solve
uses ONE window per stroke (US 8121407 claims overlapping-tile decomposition until 2030), and
there is no input-driven auto-switching between segmentation algorithms (US 10698588). The
foundations used are expired: Boykov-Jolly seeded min-cut (US 6973212), GrabCut color models
(US 7660463), MSR Paint Selection (US 8452087, fee-lapsed).
`ui_quick_select_stroke_selects_object_and_is_undoable` asserts the no-mid-drag-selection
behavior, so a live-preview change will fail it.

- The algorithm is Qt-free in `src/core/quick_select.hpp/.cpp`: RGB-histogram color models, a
  from-scratch Boykov-Kolmogorov max-flow on the 8-connected window grid (`detail::GridMaxflow`,
  validated against an Edmonds-Karp reference in
  `quick_select_maxflow_matches_reference_on_random_grids`), a seed-connectivity filter, hole
  filling of delta-enclosed regions (catchlights inside an eye; the flood must travel through
  ALL unchanged pixels or kept rings around a subtraction get eaten), and windows over ~600k px
  solved on a downsample.
- The energy was CALIBRATED AGAINST PHOTOSHOP 2026 (July 2026) by replaying identical clicks in
  both engines on `local-test-fixtures/photos/akiko-birthday.psd` (uncommitted) and scoring
  IoU; final agreement ~0.88-0.93 on object clicks (eye/tooth), ~0.6-0.8 on flat-area clicks
  where the residual is a 1-3px rim halo. PS's tool options descriptor (AM
  `currentToolOptions`: `quickSelectBrushSize`, `quickSelectSpread`, `quickSelectStickiness`,
  `autoEnhance`) confirmed the model. The load-bearing design decisions, each pinned by the
  probe data — do not "simplify" them away:
  (1) posterior = SYMMETRIC epsilon over raw frequencies (per-model Laplace floors made every
  unseen color lean foreground and flood); (2) background sampled from the solve WINDOW, not
  the whole image; (3) `kBackgroundPrior` on unbrushed pixels (neutral pixels otherwise join
  the source side for free); (4) a SPREAD BUDGET (`QuickSelectParams::spread`, PS-like, growth
  ~2.5x brush radius at 50) via a chamfer-distance ramp with a steep wall — a click on a
  featureless area returns roughly the brush disc, PS-verified; (5) boundary-dominant
  smoothness (lambda 8) with a THRESHOLDED edge classifier (smoothstep between 2x and 6x the
  window's mean pair distance, 0.15 floor) instead of the GrabCut exponential — soft shading
  gradients and noise attract nothing, real contours are cheap, which is why a click inside an
  eye takes the whole eye opening (including dissimilar catchlights) like PS; (6) the 3x3-blur
  denoise feeds ONLY the contrast term — blurring the data term smears hard boundaries and
  insets the cut ~2px (`quick_select_stroke_grabs_flat_region_and_respects_edges` catches it).
  The scratch comparison harness (qs_probe / mask_compare / a COM+computer-use PS click driver)
  lives in the session scratchpad, not the repo; the methodology is reproducible from this
  note: fixed PS window, 100% zoom, magenta fiducial calibration, masks exported via
  fill-and-saveAs with history restore.
  Tune `lambda` / `kBackgroundPrior` / spread constants only with the `quick_select_*` core
  tests green; they encode the PS-calibrated behaviors (bounded taps, big-seed full coverage,
  budget-bounded subtract shave vs covering-stroke full removal).
- Quick Select has no Intersect mode (Photoshop parity): Shift+Alt clamps to Add at press, and
  the Intersect options-bar button is simply not listed for the tool. A stroke in New mode
  auto-switches the tool to Add (`set_selection_mode(Add)` in `finish_quick_select_stroke`),
  and the MainWindow selection-mode callback writes canvas-driven changes back into
  `selection_modes_` so the mode survives tool/document switches.
- `kSelectionToolCount` is 6 (Magnetic Lasso joined July 2026); growing it again means updating
  BOTH brace-initializers (canvas_widget.hpp `selection_modes_per_tool_`, main_window.hpp
  `selection_modes_`) plus the tools array in `apply_selection_modes_to_canvas`.

## Magnetic Lasso: live-wire boundary tracing, patents all expired

The Magnetic Lasso (`CanvasTool::MagneticLasso`, Shift+L, flyout shared with the Lasso) is the
Intelligent Scissors / Live-Wire technique (Mortensen & Barrett SIGGRAPH 1995; Barrett &
Mortensen, Medical Image Analysis 1997). Patent posture, checked July 2026: the technique is
1995-1997 published prior art and the one covering patent, **US 5,995,115 (Avid), expired
2017-04-04**; Photoshop's Magnetic Lasso shipped in PS 5.0 (1998), so any Adobe patent on that
era's behavior lapsed by ~2019. Unlike Quick Select there is NO live-region constraint here —
boundary *path* tracing with live snap display is itself 1995 prior art — but the tool still
only builds the selection REGION once, in `finish_magnetic_lasso()`, keeping it clearly outside
US 8050498's brush-driven classify-and-display claims (see the Quick Select section).
`ui_magnetic_lasso_traces_edge_and_commits_selection` pins the no-mid-trace-region behavior.
The name "Magnetic Lasso" is not an Adobe trademark (descriptive; used verbatim by Photopea and
Krita's developers).

- The engine is Qt-free in `src/core/magnetic_lasso.hpp/.cpp` (`LiveWireEngine`): per-anchor
  windowed Dijkstra (Dial bucket queue, 8-connected) over a cost map of 3x3 Sobel gradient
  (per-RGBA-channel L1, channel max) + luma Laplacian zero-crossing. Integer math only, fixed
  tie-breaks, no RNG — the same cross-toolchain determinism rule as brush_dynamics. Window =
  square around the anchor (radius `max(128, 4*width)`), regrown to bbox(anchor, target) when
  the cursor escapes; over the 600k-node budget it falls back to a Bresenham line. A path whose
  every pixel sits below the Edge Contrast threshold is REPLACED by the Bresenham line — under
  the 5/7 step metric all flat-region staircases cost the same and the tie-broken Dijkstra tree
  returns an elbow, so featureless traces would look broken without this (pinned by
  `magnetic_lasso_flat_region_yields_straight_line`).
- UX is Photoshop hover tracing, not a drag: click starts, the wire follows the button-up
  pointer (mouseTracking is already on), long live segments cool into auto fastening points,
  click drops a manual anchor, double-click/Enter/click-near-start closes, Escape cancels.
  Three PS-parity rules fixed after real-image testing (July 2026), don't regress them:
  (1) Frequency -> anchor spacing is in **SCREEN pixels** (`magnetic_anchor_spacing()` is
  screen px; `cool_magnetic_live_path()` divides by `zoom_`) — document-space spacing meant
  zero fastening points at high zoom and a long live segment that re-solved and swung wildly
  (`ui_magnetic_lasso_anchor_density_follows_zoom` pins this); (2) the closing segment on
  double-click/Enter/click-near-start is **magnetic** — it runs the engine from the last point
  back to the start before teardown; Alt+double-click/Alt+Enter close straight
  (`ui_magnetic_lasso_enter_closes_along_edges` pins the no-chord behavior) — BUT with an
  **anti-retrace check**: when start and finish sit on the same edge, the cheapest magnetic
  close is the traced boundary run backwards, and winding fill collapses that polygon into two
  sliver selections; if the closing segment mostly hugs the traced path (bucket-grid overlap
  > 50%, endpoints excluded) it is dropped and the implicit straight close connects the ends
  (`ui_magnetic_lasso_line_trace_double_click_closes_straight`); (3) **manual anchors are NOT
  re-snapped** (extract with `snap_target=false`) — a manual click is the user's correction
  tool when the wire won't stick, per PS semantics. **Backspace and Delete** both pop the
  last anchor: while a trace is live, `CanvasWidget::event()` accepts the ShortcutOverride
  for those keys so the app-level `layer.clear` shortcut (Delete everywhere, plus Backspace
  on macOS) never consumes them (pinned by
  `ui_magnetic_lasso_delete_and_backspace_pop_anchors_not_layer_clear`). The combine mode
  latches at the starting click. The trace cancels on tool switch, focus loss, and edit lock;
  the engine reads a `QImage` snapshot of the trace-start composite
  (`magnetic_source_image_`), so mid-trace document edits don't refresh edges until the next
  trace (accepted). Both lassos (freehand and magnetic) commit through the `lasso_selection_mask`
  path whenever **Anti-alias** is on, not just when feathered — the QRegion path is hard-edged,
  which made deletes cut aliased stairs. The magnetic commit additionally smooths its polygon
  with two closed-loop 1-2-1 passes before rasterizing: the traced boundary is a dense integer
  pixel chain whose segments are almost all grid-aligned, so the raw chain gives the
  anti-aliaser nothing to smooth (the freehand lasso needs no smoothing — its points are sparse
  mouse samples with naturally oblique segments).
  `ui_magnetic_lasso_antialias_clear_leaves_partial_edge_pixels` pins the partial-coverage
  behavior at both the mask and the composite level.
- Width/Edge Contrast/Frequency are canvas-owned like the wand options (copied to new sessions
  in the `!used_default_tool_settings` block, re-synced by `refresh_options_bar()`, persisted
  as `tools/magneticLasso*`); `[`/`]` adjust Width while the tool is active. Coverage:
  `magnetic_lasso_*` in test_main.cpp, `ui_magnetic_lasso_*` in ui_visual_tests.cpp.

## Marching ants selection outline is traced and cached

The selection outline is no longer rebuilt per paint from QRegion edge subtractions.
`src/ui/selection_outline.*` traces the selection into closed contour loops
(`trace_selection_outlines`, marching-squares wall-follow; diagonal pixels stay
4-connected) and `CanvasWidget` caches both the document-space loops and the stroke-ready
device-space paths (`ensure_selection_outline_screen_path`, keyed by zoom/pan/viewport), so
the 120 ms animation tick only restrokes. Two consequences for future code:

- Any new code that writes `selection_` / `selection_display_region_` directly instead of
  going through `set_selection_from_region` / `set_selection_from_mask` /
  `restore_selection_before_edit` / `apply_selection_snapshot` / `reselect` must call
  `invalidate_selection_outline()` or the ants will render a stale outline.
- Below 100% zoom the outline is retraced at *device resolution* (AA coverage
  rasterisation thresholded at 50%, `trace_device_selection_outlines`), which merges or
  drops sub-pixel holes/islands exactly like the scaled-down artwork — do not "fix" that
  by tracing document space at low zoom; it strobes. Loops shorter than one 4-4 dash
  period go to a separate `pinpoint` path drawn with 2-2 dashes over the solid black
  underlay so single-pixel selections never blink invisible.

Test filters: `selection_outline` (tracer/path units) and `ui_marching_ants` (rendering).

## Reviewing a contributor PR ("let's look at this PR…")

When Seth points at a PR and wants it reviewed, this is the workflow he expects — do these
steps in order and let him drive the merge decision:

1. **Read it first.** `gh pr view <N>` and `gh pr diff <N>`, then review the change in
   context (open the touched files, verify referenced symbols/patterns actually exist and
   match surrounding code). Explicitly check for malicious or sketchy code — network calls,
   filesystem/process/shell access, new dependencies, obfuscation. Report a short verdict.
2. **Build & test it locally — never create remote branches or worktrees.** Fetch the head
   into a local branch (`git fetch origin pull/<N>/head:pr-<N>`), check it out, run the
   release build, then run the relevant test subset plus the full UI + core suites. A running
   `build\release\patchy.exe` will lock the link step (`LNK1104`); ask Seth to close it rather
   than force-killing (he may have unsaved work).
3. **Let Seth test the built `patchy.exe` himself** and wait for his go-ahead. Do **not**
   merge until he says to.
4. **Confirm the merge path before merging.** Do not default to `gh pr merge` as soon as Seth says
   to merge; ask whether he wants a straight GitHub merge or a local merge that can include
   follow-up commits first (for example missing translations found during review).
   - For a straight remote merge, use GitHub with a real merge commit:
     `gh pr merge <N> --merge`, then `git checkout main`, `git pull origin main`, and delete the
     temporary `pr-<N>` local branch.
   - For a local merge with follow-ups, `git checkout main`, `git pull origin main`, then
     `git merge --no-ff pr-<N>` so the contributor's commits keep their authorship. Add the
     follow-up commit(s), build/test the final tree, then push `main`. GitHub should still mark
     the PR merged once the PR head commit is reachable from `main`.
   Never squash contributor PRs, which would strip their commit attribution.

## Profiling stress test (PATCHY 64 scene)

A built-in, deterministic performance harness (July 2026): it closes all documents, builds the
"PATCHY 64" retro desk scene (C64 + CRT boot screen loading "LEGEND OF THE RED CAVALIER", pixel
Seth/Akiko/Ten-chan/Pon-chan at the C2 Kyoto storefront) while timing 42 stable-id steps across
paint/text/styles/filters/adjustments/move/interact/history/io, then writes reports. Entry
points: Preferences > Application > Development (warning dialog, results dialog, scene left
open) and `patchy.exe --stress-test[=quick|small|standard|huge] [--stress-report-dir <dir>]`,
which skips the splash/update check AND the whole single-instance mechanism, runs, writes
reports, and exits 0 (success) / 1 (failure or unsettled step) / 2 (bad preset token). Default
preset is **quick (1024 px, well under a minute)** for iteration; run **standard (4096 px,
several minutes)** for full-scale measurements - the move outline-preview thresholds and the
async full-refresh path only engage at standard and above. Implementation:
`src/ui/main_window_stress_test.cpp` (`StressTestRunner`, a MainWindow friend), shared types in
`src/ui/stress_test.hpp`.

- Reports land in `%APPDATA%\Patchy\stress-reports\` as `stress-<timestamp>.{json,txt}` plus
  stable `stress-latest.{json,txt}` copies (agents: read `stress-latest.json`), alongside
  `stress-scene.png` and `stress-scene.psd`. The run must happen on a REAL screen for
  meaningful numbers; the offscreen `smoke` preset (hidden, 512 px) exists only for the
  `ui_stress_test_smoke_preset_writes_report` suite test and its numbers are not comparable.
- The rating is 1000 x geometric mean of per-step baseline/actual over `kStepBaselines` in
  main_window_stress_test.cpp (>1000 = faster than baseline). To recalibrate: run standard on
  the reference machine, paste the TXT report's suggested table over `kStepBaselines`, bump
  `kBaselineTag`. Step ids are diffed across runs and key the baselines - never rename one.
- Move matrix (steps 29-33) pins the move-drag outline fallback: `RenderCacheDiagnostics`
  gained `move_outline_previews` (incremented when a drag switches to outline preview, see the
  thresholds at canvas_widget.cpp `kMoveOutlineDirtyAreaThreshold` / styled variant). At
  standard size, steps 29/32 must report 0 (live path) and steps 30/31 must report 1; per-step
  diagnostics deltas in the JSON show dirty-rect behavior (partial patches vs full refreshes).
  The scene's prop layers are cropped to their opaque pixels after painting
  (`tighten_layer_to_opaque`) because the move machinery sizes its work from RAW layer bounds -
  `add_layer()` buffers are full-canvas, which would push every drag over the outline
  threshold and misrepresent real (tight) user layers.
- Undo/redo restores go through `CanvasWidget::set_document_for_history_restore`, which keeps
  the previous frame + render diagnostics when the restored document has the same size (plain
  `set_document` dumps both). Together with the paint deferral below, history steps on big
  documents swap frames instead of flashing checkerboard.
- Text sizes in the scenario are computed in PIXELS via the shared `text_pixels_to_points`
  (main_window_shared) - the size spin takes points, converted at the document's print PPI
  (default 300, not 96; assuming 96 rendered every string ~4x too big in the first cut).
- Scene steps that would otherwise commit dozens of tool strokes (key grid, scanlines, pixel
  art) write cells directly into the layer buffer under ONE undo snapshot and invalidate per
  cell - deliberate, because `push_undo_snapshot` copies the whole Document (~0.5 GB at
  4096 px) per stroke commit. The runner trims `undo_stack` between steps for the same reason;
  the Huge preset still peaks at several GB.
- The composite checksum in the report is FNV-1a over the final flatten - comparable on one
  machine only (text AA varies across machines, and the strip-parallel renderer below makes it
  thread-count dependent too). A checksum change during optimization work means rendering
  changed, not just speed.
- Smart invalidation (July 2026, the dirty-rects round): undo/redo diffs the two history
  states per layer (globally-unique revisions + visibility; structure/document-level changes
  fall back to full) and invalidates only the changed effect-bounds region below 8 Mpx
  (`history_restore_changed_region`, main_window.cpp). The expensive per-effect style masks
  (EDT/spread/interior blurs) are cached across renders keyed by layer CONTENT revision with
  domains stored relative to the layer origin, so moves and patch renders reuse them
  (`StyleMaskProvider` in render/layer_compositor.hpp, LRU + in-flight latch in
  image_document_io.cpp; `PATCHY_STYLE_MASK_CACHE_OFF=1` disables). Channel-separable
  adjustments (Levels/Curves/Color Balance) composite through exact 256-entry LUTs
  (`build_adjustment_lut`); Hue/Saturation stays per-pixel. Cached-mask renders are only used
  where the legacy windowed domain equals the full domain (gate in DocumentStyleMaskProvider),
  so full-render bytes are unchanged - pinned compositor tests hold.
- **Reads must not bump layer revisions**: Layer's mutable accessors bump render/content
  revisions on ACCESS, so read-only code must go through const layers (`std::as_const`), or it
  silently invalidates every revision-keyed cache. Document::find_layer once bumped every
  visited layer per lookup (thousands per frame); its walk is now const + const_cast. Hunt
  regressions with `PATCHY_REV_TRACE=1` (stderr REVBUMP lines per accessor+layer).
- Full renders at 4 Mpx+ composite in parallel horizontal strips (render_document_rect,
  image_document_io.cpp; July 2026, ~4-6x on many-core machines). Style-mask float blurs are
  windowed per clip, so strip output can differ from the sequential walk by ~1-2/255 at strip
  boundaries near styled layers - the same divergence class the dirty-rect patch path already
  has vs full refreshes. Every pixel test renders below the threshold (sequential, byte-stable);
  `PATCHY_RENDER_SINGLE_THREADED=1` forces the sequential path when a byte-stable big render is
  needed (e.g. cross-run checksum comparisons). Tracing/profiling renders also stay sequential
  so per-step instrumentation remains meaningful.
- Adding a step: call `step("NN_id", "label", "category", body)` (or `fps_step` for drag
  phases) inside a phase, keep ids stable, add a `kStepBaselines` entry, and scale geometry
  through the `at()`/`motion_steps()` helpers so smoke stays fast. All user-facing strings via
  tr() + patchy_ja.ts; report content stays English on purpose (machine-readable).
- `CanvasWidget::render_settled()` (public) reports "no recomposite pending or in flight"; the
  runner's settle loop = repaint() + pump until settled. Reuse it for any future "wait until
  the canvas is truly current" need instead of sleeping.
- A progress dialog (`stressTestProgressDialog`, WindowModal) tracks the 42 steps; Cancel stops
  at the next step boundary, writes the partial report (`"cancelled": true`, exit code 1 from
  CLI), and leaves the partial scene document open and MODIFIED (the user may want to keep it),
  unlike a completed run whose scene is marked saved.
- Related paint change (July 2026, prompted by the stress test's constant canvas blanking):
  `paintEvent` defers a FULL recomposite to the fire-and-forget async refresh and keeps drawing
  the previous frame whenever `CanvasWidget::should_defer_full_refresh_to_async()` says so
  (cache dirty + same-size previous frame + no processing operation + document at or above the
  compile-time `kProcessingOverlayDirtyAreaThreshold`, 8 Mpx). Big documents no longer flash
  checkerboard on add-layer/undo/blend changes. Deliberately keyed on the compile-time constant
  (not the `PATCHY_PROCESSING_OVERLAY_MIN_PIXELS` override) so overlay-path tests keep their
  blocking semantics; sub-threshold documents render synchronously in paint exactly as before.

## Build system: runtime asset copies are shared copy-once targets

Fonts, Qt DLLs/plugins, and the Qt base translation are copied into the build directory by shared copy-once custom targets in `CMakeLists.txt` (`patchy_bundled_fonts`, `patchy_qt_runtime`, `patchy_qt_base_translations`); executables depend on them via the `patchy_copy_*` helper functions. Never attach per-target POST_BUILD copies that write into the shared output directory: all executables land in the same folder, and concurrent copies of the same destination file race under parallel Ninja (this caused intermittent release-build failures, fixed July 2026). New executables that need these assets should just call the existing `patchy_copy_*` helpers.

## Testing notes

- `patchy_ui_visual_tests.exe` must run with `QT_QPA_PLATFORM=offscreen`. The offscreen platform does **not** enumerate installed Windows fonts; register what a test needs with `QFontDatabase::addApplicationFont("C:/Windows/Fonts/<file>.ttf")`. Never call `removeApplicationFont` to clean up — invalidating an in-use font cache can hard-crash the suite.
- A registered font may not appear under its familiar GDI name: the offscreen FreeType database uses the OpenType *typographic* family, so ariblk.ttf registers as family "Arial" + style "Black", not "Arial Black". Patchy's text code resolves such names via `available_text_family_style_match` (main_window.cpp); tests should not gate on `QFontDatabase::families().contains("Arial Black")`-style checks.
- The test harness `CHECK()` macro throws. A failing CHECK while a `MainWindow` with an open inline text editor is still alive aborts the process during unwind (exit code 3, no `[FAIL]` line printed). Prefer asserting after the editor session is committed/closed, or structure tests so CHECKs that may fail do not unwind past a live editor.
- Tests save PNGs via `save_widget_artifact(...)` into `test-artifacts/` next to the binary — inspect them to visually confirm rendering behavior.
- The offscreen platform does **not** clear `QApplication::keyboardModifiers()` after a synthetic `QKeyEvent` (a modifier press sets the bit; the matching release never unsets it), and the bit persists across tests in the shared `QApplication`. So a test cannot rely on the live modifier state going clean — assert cursor/mode changes through code paths that read the event's *folded* modifiers (e.g. `CanvasWidget::eventFilter`), not `keyboardModifiers()`. See `ui_brush_alt_shows_eyedropper_cursor`, which establishes and restores the brush cursor via Alt key events (not mouse-move + live modifiers) so it is order-independent.
- The shape tools (rectangle/ellipse, pixel layers and masks) honor the brush **Opacity**/**Soft** settings and go through the single-pass signed-distance renderer in `pixel_tools.cpp` (`render_shape`/`shape_pixel_coverage`); a 1px outline keeps a crisp legacy Bresenham path. They also have marquee-style options-bar **Style** (Normal / Fixed Ratio / Fixed Size) + **Width/Height** controls (`shapeStyleCombo`/`shapeFixedWidthSpin`/`shapeFixedHeightSpin`), deliberately session-only like the marquee's (mirrored via `current_shape_style_`/`current_shape_width_`/`current_shape_height_`, never persisted to QSettings). State lives in `CanvasWidget::shape_style_`/`shape_fixed_size_`/`shape_from_center_`; ALL constraint math is `CanvasWidget::shape_drag_rect()` (consumed by the live preview, the commit path, and the status readout — a future drag-a-rect shape tool inherits it by routing through the same helper). It intentionally duplicates `marquee_selection_rect()` rather than sharing code: the marquee variant embeds selection-only concerns and is pinned by its own tests. **Alt = draw-from-center** engages at press or mid-drag; Rectangle/Ellipse are deliberately exempt from the Alt temporary-eyedropper (`tool_uses_alt_left_for_color_pick`) because the two meanings fight — the eyedropper cursor was getting stuck after Alt-centered drags (fixed July 2026; Photoshop's shape tools don't Alt-eyedrop either). The `shape_from_center_` flag is re-evaluated on press / mouse move / key events while `drawing_shape_`, exactly like the Shift square constraint. A live "W x H px" readout (`draw_drag_size_readout`) follows the drag corner for shape draws AND rect/ellipse marquee drags. Coverage: `ui_shape_tool_fixed_size_and_ratio_options_work`, `ui_shape_tool_alt_draws_from_center`, `ui_drag_size_readout_shows_dimensions`. The **Fill command** (`layerFillForegroundAction`/`fill_active_layer_with_color`) uses its *own* Fill Opacity/Soft settings — `CanvasWidget::fill_opacity()`/`fill_softness()`, controls on the Fill tool's options bar, persisted as `tools/fillOpacity`/`tools/fillSoftness`, defaulting to **100% / 0** so fills are solid by default. Soft feathers the fill inward from the selection edge via `fill_rect`'s `EditOptions::fill_softness_feather` (an inward distance-transform). Tests that use Fill only to lay down a solid setup color call `use_solid_fill_settings(canvas)` (ui_visual_tests.cpp) to force 100/0 against any persisted value.
- A click on a layer-row mask/content thumbnail can rebuild the layer row (the old row widget is deleted), so never reuse a cached row/thumbnail pointer across the press — use `click_layer_row_thumbnail(...)` in ui_visual_tests.cpp, which refetches the widget for press and release. Reusing the stale pointer is a use-after-free that flakes only when the heap reuses the freed block (this was the June 2026 "unreproducible" suite crash).
- If the visual suite dies with an access violation, the log now ends with a symbolized stack (a dbghelp vectored handler in `main`) — read it instead of re-running and hoping.
- Run a subset of visual tests by passing a name substring as the first argument (or `PATCHY_UI_TEST_FILTER`): `.\patchy_ui_visual_tests.exe ui_audio_splitter`. There is no `--test` flag.
- Per-platform skips (keep this list current): on macOS/Linux — `ui_bundled_legacy_plugin_action_applies_filter` and `ui_transparency_checkerboard_and_copy_paste_preserve_alpha` (Windows-only bundled legacy 8BF shims; the contact sheet drops their three artifacts), `ui_frameless_window_edges_resize` (native frame owns resize borders; gated on `use_custom_window_chrome()`), and the two `ui_imported_psd_box_text_line_clip_*` tests (they pin Windows Arial line metrics; CoreText/fontconfig lay lines out a few px differently). Seven imported-PSD raster-preview text tests gate on **installed Arial** via `skip_without_arial_for_psd_text_preview()` (Linux ships Liberation, not Arial; without the face the Missing Font prompt correctly appears, which offscreen cannot answer — the suite HANGS in the nested dialog loop, it does not fail). `ui_main_window_renders_color_swatches` asserts frameless/badge/window-buttons **presence on Windows and absence elsewhere**. Local-fixture (`local-test-fixtures/`) tests `[SKIP]` on the remotes because that directory is deliberately untracked.

## README screenshots are generated, not hand-captured

The images in `docs/images/screenshots/` (embedded in README.md's Screenshots gallery) are
produced by the `shot_readme_*` scenes at the end of `tests/ui_visual_tests.cpp`: offscreen,
deterministic MainWindow grabs, with floating popups/dialogs composited on by hand (each
top-level widget grabs separately) plus a soft drawn shadow. To re-do the screenshots after UI
changes, run `scripts\make-readme-screenshots.ps1` (add `-SkipBuild` if `patchy_ui_visual_tests.exe`
is already fresh): it runs the `shot_readme` test filter and copies the PNGs into
`docs/images/screenshots/` with the `shot_readme_` prefix stripped. Commit the updated PNGs.

- The scenes read `local-test-fixtures/psd/akiko_cycling_okinawa.jpg` and `ipad_main_v04.psd`
  (deliberately uncommitted), so they `[SKIP]` on machines without those fixtures; regenerating
  the full set needs this machine.
- The scenes are registered in the normal test table, so the full suite keeps them compiling
  and passing; treat a `shot_readme_*` failure like any other UI test failure.
- Scene layout notes: popup/dialog positions are explicit `move()` offsets (the offscreen
  platform's small virtual screen otherwise clamps popups over the menu bar), and every scene
  ends by resetting the status bar to "Ready" so transient messages don't leak into the shots.
- Gallery table gotcha: every `<td>` in the README screenshots table carries `width="33%"`
  and `valign="top"`. Without the width, GitHub caps the table at the page width and
  distributes column width by caption length, so columns with short captions render visibly
  smaller thumbnails even though every `<img>` says `width="270"`. Without the valign, cells
  default to vertical-align middle, so thumbnails sit at different heights when captions wrap
  to different line counts. Keep both attributes on any new cells.

## Photoshop compatibility verification

Adobe Photoshop 2026 is installed on this machine and is the ground truth for PSD compatibility work. It is COM-scriptable from PowerShell: `(New-Object -ComObject Photoshop.Application).DoJavaScript($jsx)` (the first call launches Photoshop, ~30s).

- To learn how Photoshop encodes a setting, save two PSDs differing in exactly one UI toggle and byte-diff them. This is how the layer-mask link flag (mask flags bit 0 = unlinked) and the "use global light" handling (`uglg` + image resources 1037/1049) in `src/psd/psd_document_io.cpp` were pinned down in June 2026.
- Photoshop semantics established the same way, encoded in code + tests: layer record flags bit 3 ("Photoshop 5.0 and later") must be written on every layer — without it Photoshop applies legacy semantics and badly misrenders layers that combine an unlinked mask with effects. The layer mask shapes layer effects (shadow/stroke/glow sources) regardless of the link state — the chain toggle affects move behavior only, never rendering. Effect *output* may still spill onto mask-hidden areas unless the "Layer Mask Hides Effects" blending option is on (tagged block 'lmgm', 4 bytes, first byte = bool; modeled as `LayerStyle::layer_mask_hides_effects` and exposed in the layer style dialog's Blending Options page). Beware confounded controls when byte-bisecting: an early conclusion here was wrong because the "control" file lacked bit 3 and went through Photoshop's legacy path.
- lfx2 effect **blend modes must be written as full stringIDs** ("multiply", "screen", ...) in the 'BlnM' enum — Photoshop 2026 silently reads 4-char codes ('Mltp', 'Scrn') as Normal (pinned July 2026 by byte-patching probe PSDs; a 16-mode sweep through PS verified every mode Patchy writes). The parser accepts both forms via `blend_mode_from_descriptor_enum`; the writer emits stringIDs (`blend_mode_descriptor_value`). Additionally, the **GrFl (gradient overlay) descriptor is shape-sensitive**: PS resets its blend mode to Normal unless the descriptor mirrors PS's own 14-item layout (`enab, present, showInDialog, Md, Opct, Grad, Angl, Type, Rvrs, Dthr, gs99, Algn, Scl, Ofst`) — other effect descriptors are not shape-sensitive (drop shadow/outer glow blend modes survive with Patchy's leaner layouts). Gradient stop midpoints (`Mdpn`) are not modeled: read as default, written as 50 — a PS file using non-default midpoints loses them through a Patchy re-save (known limitation).
- The Stroke layer effect (July 2026) renders as an exact-Euclidean **distance band** from the
  binary contour ({alpha > 0}), not a morphological dilation: `stroke_alpha_mask` in
  `src/render/layer_compositor.hpp` computes per-pixel coverage
  `clamp(a·cov(d_in, band_in) + (1-a)·cov(d_out, band_out))` with
  `cov(d, band) = clamp(band + 1 - d)` (`kStrokeContourOffset = 1.0`; the contour sits 0.5px past
  the last pixel center and the AA ramp is 1px). Position bands: Outside = size out, Inside =
  size in, **Center = size/2 each way** (the old code dilated the full size both ways, drawing
  Center at double width; that was the July 2026 bug). The sum combine (not max) keeps the band
  seamless over anti-aliased contour pixels. Calibrated against Photoshop 2026 COM renders: band
  runs match PS run-for-run at every position on rects and circles, sizes 5-20
  (`test-fixtures/psd/photoshop-stroke-positions.psd` + `psd_photoshop_stroke_positions_fixture_matches`
  pin this; the probe scripts live in the session scratchpad). The EDT helper is
  `exact_squared_distance_transform` (Felzenszwalb-Huttenlocher, deterministic double envelope
  math, same no-toolchain-variance rule as brush_dynamics). Known limitation: on uniform
  semi-transparent fills PS composites Inside/Center strokes with a content **knockout** (content
  is replaced by stroke within its own alpha, e.g. a 50%-alpha rect with an inside stroke renders
  green-over-background across the whole shape); Patchy over-composites instead, so such interiors
  show a tinted wash. Exact knockout needs a per-layer compositing buffer. Do not "fix" it by
  binarizing the inside field at {alpha < 1}: that breaks the PS-exact AA-edge band positions
  (verified against the circle probes both ways). Opaque content (text, shapes) matches PS
  essentially exactly, which is the case that matters.
- The drop-shadow **Spread** expands the matte geometrically before blurring (COM-probed July 2026
  with spread 0/50/100 renders): solid with rounded Euclidean corners out to spread% x size, then
  blurred by the remaining (1 - spread%) x size. `prepare_layer_style_soft_mask` implements this via
  `expand_layer_style_mask_in_place` (chamfer distance, 1px ramp, stroke-band contour convention).
  Never reimplement spread as a post-blur gain: saturating the box blur's tail exposes the kernel's
  rectangular support as per-glyph boxes jutting out of the shadow (the qual_rca_pinout.psd
  spread-100 label-plate bug, fixed July 2026). Spread 0 keeps the historical triple-box blur
  profile bit for bit (it matches Photoshop already);
  `compositor_drop_shadow_full_spread_keeps_rounded_support` pins the rounded support.
- Inner-shadow / inner-glow **Choke** is the interior mirror of Spread (COM-probed July 2026 with
  choke 0/50/100 renders): the inverse matte expands geometrically to choke% x size and only the
  remaining (1 - choke%) x size is blurred, so choke 100 is a hard Euclidean band exactly `size`
  deep (Patchy matches the PS profiles within 1/255 on axes and diagonals). The inner glow's
  **Center** source erodes the matte the same way (choke 100 = hard erosion by the full size).
  `prepare_layer_style_interior_falloff_mask` (layer_compositor.hpp) implements this; choke 0
  keeps the historical interior blur (3 box passes of size/2, reach ~1.5 x size vs Photoshop's
  ~size) bit for bit, so mid chokes still overreach PS a little — deliberate, choke 0 is pinned.
  The pre-fix code applied choke as a post-blur gain ((1 - blur) / (1 - choke)), which amplified
  the box kernel's square support into interior boxes and half-tone dust (and ignored Center-source
  choke entirely); `compositor_inner_shadow_full_choke_keeps_rounded_interior`,
  `compositor_inner_glow_full_choke_keeps_rounded_interior`, and
  `compositor_inner_glow_center_choke_erodes_matte_geometrically` pin the geometric behavior.
  Outer glow's spread was already geometric (`distance_falloff_mask`) — but note its smoothstep
  falloff is heavier near the contour than PS's blur-based falloff (known divergence, unchanged).
- To check how Photoshop interprets a Patchy-written file, query Action Manager getters, e.g. `executeActionGet` of a layer reference and read `userMaskLinked` / `userMaskEnabled`.
- To compare renders, export Photoshop's flattened view and diff it against `Compositor::flatten_rgb8` of the same file. Gotcha: Photoshop's `doc.saveAs`/`doc.duplicate` fail with a misleading "disk error (-1)" on documents whose smart-object layers ('PlLd'/'SoLd' blocks) reference missing document-global 'lnk2' data — pre-June-2026 Patchy builds produced such files by dropping the global tagged-block section (now preserved; dangling references are stripped on save). For such damaged files, `doc.selection.selectAll(); doc.selection.copy(true)` (merged), paste into a fresh document, flatten, and save a 24-bit BMP from there. Single composite pixels can be probed without exporting via `doc.colorSamplers` (max 4 exist at once — add/read/remove in a loop).
- Script hygiene: set `app.displayDialogs = DialogModes.NO`, only close documents the script opened, and close with `SaveOptions.DONOTSAVECHANGES`.
- Small Photoshop-authored regression fixtures are committed under `test-fixtures/psd/` (e.g. `photoshop-unlinked-mask.psd`, `photoshop-global-light-shadow.psd`). Generate new ones with a COM script rather than hand-crafting bytes.
- Quick scratch tools that link Patchy's release libs (e.g. to flatten a PSD through Patchy's reader outside the test suite) compile with:

  ```powershell
  cmd /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c++20 /EHsc /O2 /MD /I <repo>\src <tool>.cpp /link /LIBPATH:<repo>\build\release patchy_psd.lib patchy_render.lib patchy_core.lib patchy_color.lib patchy_filters.lib gdi32.lib user32.lib advapi32.lib dwrite.lib'
  ```

  `/MD` is required to match the libs; `advapi32`/`dwrite` are needed by `psd_document_io`'s font-resolution code.

For code changes, ALWAYS finish work in this repository by refreshing the local release build — `build\release\patchy.exe` must be freshly built from the final working tree at handoff, never stale. If the change is documentation-only or otherwise cannot affect compiled/runtime behavior (for example README text/images, comments, or agent instructions), do not run the full release build/test handoff just for that change; instead report that the release handoff was skipped because the change is non-code.

Required release handoff steps:

1. Build the release preset:

   ```powershell
   cmd /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset release'
   ```

   `build\release\CMakeCache.txt` carries hand-edited flags the preset does not set: `CMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /Zi` and `CMAKE_EXE_LINKER_FLAGS_RELEASE=/DEBUG:FULL /INCREMENTAL:NO`. They make release builds emit `patchy.pdb`, which is what symbolizes user crash dumps (WER minidumps land in `%LOCALAPPDATA%\CrashDumps`). If you reconfigure from scratch, pass them again with `-D...`; to symbolize a dump from an older build, rebuild that commit in a temp `git worktree` with the same flags (full links reproduce the binary layout) and read the dump against that PDB.

2. Run the release test binaries from `build\release`:

   ```powershell
   .\patchy_core_tests.exe
   $env:QT_QPA_PLATFORM='offscreen'; .\patchy_ui_visual_tests.exe
   ```

3. In the final response, explicitly report whether the release executable exists at:

   ```text
   build\release\patchy.exe
   ```

4. If the change touched platform-guarded code, CMakeLists/presets, or packaging, additionally
   run the affected remote build(s) best-effort — `scripts\remote\remote-build.ps1 -Target mac`
   and/or `-Target linux` — and report the result. A mac/linux failure does not block the
   handoff (Windows is the gate), but it must be reported, not silently skipped.

Do not say a release was created unless the release preset build completed successfully.
