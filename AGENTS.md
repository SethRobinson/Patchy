# Repository Instructions

Keep this file current: when a change makes anything in here stale (build steps, test conventions, gotchas), update it in the same change. Multiple different coding agents read this file — it is the shared knowledge channel, so write notes for any AI, not a specific one.

When bumping the release version, update all three version fields: `CMakeLists.txt` (`project(... VERSION x.y)`), `vcpkg.json` (`version-semver`), and `latest_version.json` (the windows `version` — this is the update-check manifest, served to the app from raw.githubusercontent.com on main, so it only takes effect once pushed). Also add a new top entry under `README.md`'s "What's New" section for that version, dated with the release date, summarizing the user-visible changes.

When adding or changing user-facing English text, make sure it is wired through Patchy's localization system and update the Japanese translation in `translations/patchy_ja.ts` in the same change.

Keyboard shortcuts for QActions must be registered through `MainWindow::register_hotkey(action, "stable.id", default_seq)` (backed by `HotkeyRegistry` in `src/ui/hotkey_registry.hpp`) — never call `setShortcut`/`setShortcuts` directly on an app-level action. The registry applies user customizations from the `hotkeys/` settings group (only deltas are stored; unmodified commands track new defaults automatically) and feeds the Preferences > Hotkeys editor. Command ids are persisted in user settings, so never rename one. Two commands must not share a default shortcut — Qt silently deactivates ambiguous application shortcuts; the `ui_hotkey_defaults_have_no_conflicts` visual test enforces this.

If tests need files from outside the project directory, copy those files into `local-test-fixtures` first and have the tests read them from there. Do not add hardcoded external drive paths such as `C:\temp` or `D:\projects` to test code.

Keep git commit messages to one or two lines — a concise subject, no multi-paragraph body enumerating every change.

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
  with warnings instead of failing the file. The descriptor parser + `decode_packbits` were
  extracted from psd_document_io.cpp into the shared `src/psd/psd_descriptor.*` — PSD and ABR
  both use them.
- The user library is `src/ui/brush_tip_library.*`: `<settings dir>/brushes/` (i.e.
  `%APPDATA%/Patchy/brushes/`) with one grayscale PNG (coverage mask) + one JSON sidecar
  (`{"name", "spacing", "folder"}`) per tip; ids are the UUID filename stems. Corrupt files skip
  that tip only. Tests point the library at a temp dir via the constructor arg. Tips carry an
  organizational **folder** (empty = ungrouped, sorted first); `import_abr` files everything
  into a folder named after the .abr; `remove_tips()` bulk-deletes with a single `changed()`.
- **Built-in default tips** (`src/ui/default_brush_tips.*`): 16 parametrically generated tips
  (chalk, charcoal, pastel, bristle, sponge, canvas, smoke, spray, spatter, stipple, ink splat,
  grunge, square, calligraphy, star, grass) built from deterministic seeded noise/SDF math —
  no binary assets. `MainWindow::brush_tip_library()` seeds them into the "Patchy Defaults"
  folder gated by the `brushes/defaultTipsVersion` settings int (bump to seed new additions;
  a user deleting them is respected — no emptiness check). UI tests suppress seeding via
  `clear_brush_tip_test_state()` setting the version high; the contact-sheet test
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
  size 12, 100% opacity, 20% soft; `default_startup_brush_preset_id` applied in
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
- The **Soft** setting applies to bitmap tips as an outward edge feather:
  `patchy::soften_scaled_brush_tip` (3× separable box blur, pads the stamp and shifts the
  anchor), driven by `CanvasWidget::scaled_brush_tip_for(size, softness)` whose cache is keyed
  by (size, feather). Feather = size × softness% / 400. Note a soft tip's center coverage can
  drop below 100% for thin tips — soft erase leaving residue is correct behavior.
- Brush size maxes at **512** (canvas clamps and the options-bar spin/slider).
- Deleted default tips are recoverable: `BrushTipLibrary::restore_default_tips()` re-adds
  missing ones (matched by name within the defaults folder); the manager has a "Restore
  Default Brushes" button and first-run seeding reuses the same function under the
  `brushes/defaultTipsVersion` gate.
- v1 limitations (deliberate): layer-**mask** painting and Clone/Smudge stay procedural; ABR
  dynamics (scatter/jitter/texture/dual brush) are not imported — only tip bitmap, name,
  spacing.
- Fixture: `test-fixtures/abr/myer-settlement-brushes.abr` (CC0, see NOTICE.txt; 148 brushes,
  v6.2). Bigger CC0 sets for manual testing live in `local-test-fixtures/abr-sets/`. Coverage:
  `brush_tip_*`/`tool_brush_tip_*`/`abr_*` in test_main.cpp, `ui_brush_tip_*` in
  ui_visual_tests.cpp.

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

## Build system: runtime asset copies are shared copy-once targets

Fonts, Qt DLLs/plugins, and the Qt base translation are copied into the build directory by shared copy-once custom targets in `CMakeLists.txt` (`patchy_bundled_fonts`, `patchy_qt_runtime`, `patchy_qt_base_translations`); executables depend on them via the `patchy_copy_*` helper functions. Never attach per-target POST_BUILD copies that write into the shared output directory: all executables land in the same folder, and concurrent copies of the same destination file race under parallel Ninja (this caused intermittent release-build failures, fixed July 2026). New executables that need these assets should just call the existing `patchy_copy_*` helpers.

## Testing notes

- `patchy_ui_visual_tests.exe` must run with `QT_QPA_PLATFORM=offscreen`. The offscreen platform does **not** enumerate installed Windows fonts; register what a test needs with `QFontDatabase::addApplicationFont("C:/Windows/Fonts/<file>.ttf")`. Never call `removeApplicationFont` to clean up — invalidating an in-use font cache can hard-crash the suite.
- A registered font may not appear under its familiar GDI name: the offscreen FreeType database uses the OpenType *typographic* family, so ariblk.ttf registers as family "Arial" + style "Black", not "Arial Black". Patchy's text code resolves such names via `available_text_family_style_match` (main_window.cpp); tests should not gate on `QFontDatabase::families().contains("Arial Black")`-style checks.
- The test harness `CHECK()` macro throws. A failing CHECK while a `MainWindow` with an open inline text editor is still alive aborts the process during unwind (exit code 3, no `[FAIL]` line printed). Prefer asserting after the editor session is committed/closed, or structure tests so CHECKs that may fail do not unwind past a live editor.
- Tests save PNGs via `save_widget_artifact(...)` into `test-artifacts/` next to the binary — inspect them to visually confirm rendering behavior.
- The shape tools (rectangle/ellipse, pixel layers and masks) honor the brush **Opacity**/**Soft** settings and go through the single-pass signed-distance renderer in `pixel_tools.cpp` (`render_shape`/`shape_pixel_coverage`); a 1px outline keeps a crisp legacy Bresenham path. The **Fill command** (`layerFillForegroundAction`/`fill_active_layer_with_color`) uses its *own* Fill Opacity/Soft settings — `CanvasWidget::fill_opacity()`/`fill_softness()`, controls on the Fill tool's options bar, persisted as `tools/fillOpacity`/`tools/fillSoftness`, defaulting to **100% / 0** so fills are solid by default. Soft feathers the fill inward from the selection edge via `fill_rect`'s `EditOptions::fill_softness_feather` (an inward distance-transform). Tests that use Fill only to lay down a solid setup color call `use_solid_fill_settings(canvas)` (ui_visual_tests.cpp) to force 100/0 against any persisted value.
- A click on a layer-row mask/content thumbnail can rebuild the layer row (the old row widget is deleted), so never reuse a cached row/thumbnail pointer across the press — use `click_layer_row_thumbnail(...)` in ui_visual_tests.cpp, which refetches the widget for press and release. Reusing the stale pointer is a use-after-free that flakes only when the heap reuses the freed block (this was the June 2026 "unreproducible" suite crash).
- If the visual suite dies with an access violation, the log now ends with a symbolized stack (a dbghelp vectored handler in `main`) — read it instead of re-running and hoping.
- Run a subset of visual tests by passing a name substring as the first argument (or `PATCHY_UI_TEST_FILTER`): `.\patchy_ui_visual_tests.exe ui_audio_splitter`. There is no `--test` flag.

## Photoshop compatibility verification

Adobe Photoshop 2026 is installed on this machine and is the ground truth for PSD compatibility work. It is COM-scriptable from PowerShell: `(New-Object -ComObject Photoshop.Application).DoJavaScript($jsx)` (the first call launches Photoshop, ~30s).

- To learn how Photoshop encodes a setting, save two PSDs differing in exactly one UI toggle and byte-diff them. This is how the layer-mask link flag (mask flags bit 0 = unlinked) and the "use global light" handling (`uglg` + image resources 1037/1049) in `src/psd/psd_document_io.cpp` were pinned down in June 2026.
- Photoshop semantics established the same way, encoded in code + tests: layer record flags bit 3 ("Photoshop 5.0 and later") must be written on every layer — without it Photoshop applies legacy semantics and badly misrenders layers that combine an unlinked mask with effects. The layer mask shapes layer effects (shadow/stroke/glow sources) regardless of the link state — the chain toggle affects move behavior only, never rendering. Effect *output* may still spill onto mask-hidden areas unless the "Layer Mask Hides Effects" blending option is on (tagged block 'lmgm', 4 bytes, first byte = bool; modeled as `LayerStyle::layer_mask_hides_effects` and exposed in the layer style dialog's Blending Options page). Beware confounded controls when byte-bisecting: an early conclusion here was wrong because the "control" file lacked bit 3 and went through Photoshop's legacy path.
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

Do not say a release was created unless the release preset build completed successfully.
