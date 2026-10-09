# Scripting API compatibility

2026-10-09 additive (API 1): `patchy.scripts` (`PatchyScripts` in patchy.d.ts): `userFolder`,
`bundledFolder`, `list()` / `rescan()` (every script with its effective `hotkey`,
`defaultHotkey` and `commandId`), `install(relativePath, source, {hotkey})` (writes below the
user folder, rescans, binds the Preferences override), `setHotkey` / `getHotkey`. The File >
Scripts entries gain the Script Manager's right-click menu. Pinned by
`ui_script_library_installs_and_binds_hotkey` and `ui_scripts_menu_context_menu_offers_script_actions`.

2026-10-09 additive (API 1): `doc.saveAs(path, options?)` and `doc.exportAs(path, options?)`
take the format's save options (`PatchySaveOptions` in patchy.d.ts: JPEG/WebP/JXR/RTTEX
`quality`, WebP/JXR/PDF `lossless`, DDS `compression` and `mipmaps`, ICO/CUR `sizes`,
`resample` and `hotspot`, BMP `encoding`, `paletteMode` and `palettePath`, RTTEX `encoding`,
`powerOfTwo`, `forceSquare`, `forceAlpha` and `compress`, the exportPdf keys, GIF `animate` and
`frameDelayMs`). Unspecified keys follow the user's Save Options defaults; an unknown key, a
wrong type, an out-of-range value, or a key for another extension throws before anything is
written. A scripted save no longer rewrites the user's persisted saveOptions defaults, and
scripted saves skip the saved-channels and Aseprite fill-opacity prompts (the script decided).
Pinned by `ui_script_save_as_options_write_dds_and_jpeg` and `ui_script_save_as_options_are_strict`.

2026-10-09 behavioral correction (API 1): `doc.exportAs` is a copy export. It used to be
identical to `saveAs`, so a flat document exported as .dds became the .dds session; now the
document's path, title and modified state never change, whatever the format. Scripts that
relied on exportAs retargeting the document should call saveAs.

2026-10-09 additive (API 1): per-script hotkeys. A `// @hotkey Ctrl+Alt+D` header line is a
script's default shortcut; Preferences > Hotkeys lists scripts under "Scripts" and its
override wins. The command id is `script.` plus the percent-encoded relative path
(`script.Utilities%2Fquick-export-dds.js`), listed by `app.commandIds()`; `app.runCommand`
refuses those ids because one script runs at a time. New bundled example
`Utilities/quick-export-dds.js`. See docs/scripting.md "Script hotkeys".

2026-10-09 additive (API 1): `addTextLayer(text, {area})` creates area text from one
closed `PatchyVectorPath` in document coordinates. `area` and `box` are mutually
exclusive. `layer.textArea` reads a detached boundary snapshot or `null`; assigning
a boundary reflows through the text session, and assigning `null` converts it to
box text. Source shapes remain independent. See docs/area-text.md.

2026-10-09 additive (API 1): `doc.bitDepth` (8, 16 or 32) and `doc.convertBitDepth(bits)`
(Image > Mode's conversion; throws for other values, while 16 and 32-bit editing is off,
or on an Indexed document). On 16 and 32-bit documents `getPixels` and `setPixels` stay
RGBA8 and convert at the boundary, `fill` and `fillRect` write at the document's depth,
and `applyFilter` runs at depth, throwing for a filter a 32-bit document does not offer.
See docs/high-bit-depth.md; pinned by `ui_script_bit_depth_and_deep_filters`.

2026-10-07 additive (API 1): `doc.resizeImage(width, height, {method})` takes a resampling
method id: `"automatic"` (default), `"nearest"`, `"bilinear"`, `"bicubic"`, `"bicubicSmoother"`,
`"bicubicSharper"`; an unknown id throws. Behavioral correction in the same change: a resize
without a method is Automatic (Bicubic Sharper for a reduction, Bicubic Smoother for an
enlargement) instead of the old fixed bilinear, 16-bit and float documents are filtered instead
of nearest-sampled, reductions average their footprint, and alpha interpolates premultiplied. The
Image Size dialog now honors its method combo and remembers the choice. See docs/resampling.md;
pinned by `ui_script_resize_image_method_option` and the `resample_*` core tests.

2026-10-06 behavior (API 1): `ui.zoom` reads and writes the view zoom in percent, where
100 is one document pixel per device pixel (the status-box number). On a HiDPI or scaled
display the value therefore differs from the logical widget scale by the device pixel
ratio; at ratio 1 nothing changes. Matches Photoshop's 100% (GitHub issue 75).

2026-10-05 additive (API 1): `layer.rerenderSmartObject()` renders an embedded smart
object again from the file it stores, for every layer sharing that source, and returns
the number of layers re-rendered. A smart object opened from a PSD shows the pixels
saved in the file until it is transformed or its contents change. Throws for a linked
smart object (`updateSmartObject()` is the call for those), a plain layer, a locked one,
or contents that cannot be decoded. Pinned by `ui_script_rerender_smart_object_from_embedded_file`.

2026-10-05 additive (API 1): `layer.rerenderText()` renders a text layer again from its
stored text, fonts and formatting, changing none of them. A type layer opened from a PSD
shows the pixels saved in the file until it is edited; this replaces them with Patchy's
own render (what Testy uses to score Patchy's text engine instead of Photoshop's cached
pixels). Throws on a layer that is not text or is locked. Pinned by
`ui_script_rerender_text_replaces_stored_pixels`.

2026-10-03 additive (API 1): `doc.mergeLayers(layers, {singleVector: true,
effectsFrom?: layer})` explicitly combines selected vectors at the bottommost
source's stack position. It removes individual layer effects unless `effectsFrom`
selects one source stack to apply to the combined silhouette. Fills, vector strokes
and curves remain editable. Protected or incompatible inputs throw before mutation.
See [layer-merging.md](layer-merging.md); pinned by `ui_layer_merge_single_vector_*`.

2026-10-02 behavioral correction (API 1): text font warnings recognize compact family
spellings such as `LiberationSans` as the same font as `Liberation Sans`. Creating or
editing text with that spelling no longer reports a missing font when the requested
face renders it. Pinned by `ui_text_name_table_names_resolve_to_the_registered_face`.

2026-10-02 additive (API 1): `doc.exportAnimatedWebp(path, options?)` writes visible
top-level layers as animation, with millisecond timing, finite or infinite play counts,
quality and lossless options. It preserves the source document path and dirty state.
Defaults and validation are in `scripts/bundled/patchy.d.ts`; ordinary `saveAs` and
`exportAs` WebP output stays flat. MCP uses the same method via `execute_script`.

2026-10-01 behavioral correction (API 1): text font warnings name their cause, and the text
setters log them too. A font that is installed but has no glyph for any character of the text
(the bundled Noto Naskh Arabic asked for Latin text) now logs `addTextLayer: font has no
glyphs for this text, rendered with a fallback: <family>`. It used to log `font not
available`, which still appears for a font that is not installed. A layer with both problems
logs both lines. `layer.text`, `setTextRuns`, `textAlign`, `textParagraph`, `textOrientation`
and `textDirection` used to say nothing; they now log the same two lines under their own name
(`layer.text: font not available, ...`). That includes the case where the edit replaced a
missing font with a substitute, so `textFont` no longer names the font the layer was created
with. Runs that give every character an installed font log nothing. These are console lines,
never dialogs. Pinned by `ui_script_text_font_without_glyphs_warns_with_the_real_cause` and
`ui_script_text_setters_warn_about_fonts`.

2026-10-01 behavioral correction (API 1): `layer.moveTo` and the `x` / `y` setters move a
layer the way the Move tool does. They used to shift only the pixel bounds and the mask
bounds, leaving the placement data behind: a shape kept its old path and geometry and
snapped back at its next re-render, a smart object kept its old quad, and a text layer kept
its old transform, so Photoshop laid it out again at the creation anchor on the first edit.
Now the shape model, smart-object quads, text transform, preserved vector-mask data and a
linked raster or vector mask travel with the layer and with every layer inside a moved
group, and a smart object with Smart Filters re-renders at the new place. One visible
difference: an unlinked mask now stays where it is (it used to move with the layer).
Pinned by `ui_script_move_*`; the Photoshop check is
`scripts\dev\photoshop-text-move-check.ps1`. See [scripting.md](scripting.md).

2026-09-30 (API 1): smart objects. `doc.addSmartObject(path, {linked?, x?, y?, width?,
height?, scale?, name?})` places a file as an embedded or linked smart-object layer
(the core behind File > Place Embedded and the new File > Place Linked); a linked
placement of a file the document already links shares that source. Layers expose
`isSmartObject`, `getSmartObject()` (`{linked, fileName, path, relativePath, missing,
changed, sourceId, width, height, resolution, quad}` or null) and `updateSmartObject()`
(Update Smart Object Content for every layer sharing the source; returns the count).
Additive; apiVersion unchanged. See [smart-object-editing.md](smart-object-editing.md).

2026-09-28 (API 1): `patchy.plugins.folder` (the plug-ins folder next to the application,
created with its README on read; "" off Windows) and the `captureDialog` option of
`layer.applyPlugin` (a PNG of the plug-in's own dialog while it is up). Additive;
apiVersion unchanged. See [plugins.md](plugins.md).

2026-09-28 (API 1): legacy Photoshop plug-ins. `patchy.plugins` (`list()`, `rescan()`,
`folders` get/set) exposes the `.8bf` filters found in the plug-in folders, and
`layer.applyPlugin(id, {dialog?})` runs one on a pixel layer inside the selection as one
undoable edit (`{dialog: false}` skips the plug-in's own dialog; unattended runs never show
it). Windows only; elsewhere every entry lists as unsupported and `applyPlugin` throws.
Additive; apiVersion unchanged. See [plugins.md](plugins.md).

2026-09-27 (API 1): paragraph metrics. Text layers expose `textParagraph` (read/write:
`{firstLineIndent, startIndent, endIndent, spaceBefore, spaceAfter}` in document pixels;
reading gives the first paragraph, setting merges the given fields into every paragraph), and
`doc.addTextLayer` takes the same object as its `paragraph` option. Additive; apiVersion
unchanged. See [text-tool.md](text-tool.md).

2026-09-26 (API 1): rich text. `doc.addTextLayer` accepts an array of runs (`{text, font?,
size?, bold?, italic?, color?}`) in place of the string, so one layer mixes faces, sizes and
colors; options gain `box` (`{width, height}`: a wrapping paragraph text box with x/y as its
top-left corner) and `align`. Text layers expose `textRuns` (the stored runs), `textBox`
(`{width, height}` or null), `textAlign` (read/write) and `setTextRuns(runs)`, which retypes
the layer with formatted runs on top of the first character's formatting. Additive;
apiVersion unchanged. See [text-tool.md](text-tool.md).

2026-09-26 (API 1): `doc.addTextLayer` renders exactly the face its options name. The
session seeded its face from the options bar's style picker, so with the bar parked on a
Semibold or Black layer every scripted layer in a family offering that face took it,
whatever `font`, `bold` and `italic` said. The requested `size` is now committed exactly
at every canvas zoom, and an unchanged `layer.text` re-edit keeps the size (the whole-pixel
editor font divided by a low zoom used to shift it by a pixel or two). On Windows `font`
also accepts a face's full name or PostScript name ("Futura Extra Black BT",
"FuturaBT-ExtraBlack") for the face the database lists as family + style. Behavioral fixes;
apiVersion unchanged. See [text-tool.md](text-tool.md).

2026-09-26 (API 1): `app.listFonts()` returns every family the text engine can use as
`{family, styles, writingSystems}` objects sorted by family, loading the installed
fonts first under `--headless` on Windows. Additive; apiVersion unchanged.

2026-09-26 (API 1): `doc.addTextLayer`'s `font` option now takes effect. The script path
set the family on the editor's character format only, while the commit read the session's
family, so every script-made text layer rendered in the options bar's current font. A
family that is not installed now logs a console warning naming it, and text layers expose
a read-only `layer.textFont` (the stored family name, `""` for other layers). Behavioral
fix plus an additive property; apiVersion unchanged. See [text-tool.md](text-tool.md).

2026-09-25 (API 1): `patchy.recovery` exposes the automatic document recovery store:
`enabled` and `intervalMinutes` (the Preferences values), `directory`, `writeNow()`,
`listFiles()`, `listOrphaned()`, `recoverAll()`, and `discardOrphaned()`. Additive;
apiVersion unchanged. See [document-recovery.md](document-recovery.md).

2026-09-25 (API 1): `doc.importFilesAsLayers(paths)` adds image files as layers directly
above the active layer, bottom to top in argument order (the core behind File > Import >
Files as Layers, the Layers-panel file drop, and Paste with copied files). A multi-layer
file becomes a folder named after it; an unreadable file throws without adding anything.
Additive; apiVersion unchanged. See [import.md](import.md).

2026-09-25 (API 1): the `layer.text` setter replaces the text the way retyping it in the
editor does, so the new text keeps the first character's run formatting (exact fractional
size, Character-panel glyph scales, leading, tracking, faux styles). It used to delete the
text first and re-insert at the session's fallback font, so an imported Photoshop layer
with a 0.93 vertical glyph scale re-rendered 7.5% taller than the same layer applied
interactively. Behavioral fix; apiVersion unchanged. See [text-tool.md](text-tool.md).

2026-09-24 (API 1): `layer.removeObject(options?)` gains `toneMatch` (0..100, default
0, the raw exemplar fill), `feather` (px, default 0; softens the fill's edge
outward), and, for the content-aware method, `attempt` as the variation number (0 = the
best-match fill, each n > 0 a different reproducible fill, the dialog's Reroll). The
result gains `attempt`. Existing calls are unchanged. Additive; apiVersion unchanged.
See [healing.md](healing.md).

2026-09-22 (API 1): `doc.alignLayers(edge, options?)` and `doc.distributeLayers(mode,
options?)` run Layer > Arrange > Align / Distribute (`edge` ids `left`, `hcenter`, `right`,
`top`, `vcenter`, `bottom`; Distribute adds `hspacing`, `vspacing`; options `layers` and,
for Align, `alignTo: "selection" | "canvas"`). Both return the number of layers moved and
ride the run's single undo entry. Additive; apiVersion unchanged. See
[alignment.md](alignment.md).

2026-09-22 (API 1): `layer.removeObject(options?)` runs Edit > Remove Object on the
document selection. `{method}` is `"contentAware"` (default, the exhaustive exemplar
fill) or `"nearestEdge"` (the selection form of Spot Healing, where `{attempt}` picks
the source candidate); the call returns `{method, patches, source, sourceCount}`. The
layer must be the active layer. Additive; apiVersion unchanged. See [healing.md](healing.md).

2026-09-21 (API 1): `app.exportPdf(documents, path, options?)` writes a multi-page PDF
with one page per document (`lossless`, `editableLayers`, `missingFontsAsImages`
options), the same writer as File > Export Multi-Page PDF. Additive; apiVersion
unchanged. See [pdf.md](pdf.md).

2026-09-20 (API 1): vertical type and paragraph direction. `doc.addTextLayer` takes
`orientation` (`"horizontal"` | `"vertical"`) and `direction` (`"auto"` | `"ltr"` |
`"rtl"`); text layers expose `textOrientation` and `textDirection` (read/write, a write
re-renders through the same hidden session as `text`). Invalid values throw. Additive;
apiVersion unchanged. See [text-tool.md](text-tool.md).

2026-09-11 (API 1): `layer.duplicate(targetDocument?)` accepts another open
document and copies the layer there, above its active layer at the same
coordinates (centered when the sizes differ); masks, styles, and smart-object
sources travel with it. Without an argument the behavior is unchanged.
Additive; apiVersion unchanged. See [layer-panel.md](layer-panel.md).

2026-09-09 (API 1): RAW filename opens through `app.open` and MCP read the photo's
`.rawprefs` sidecar, falling back to current defaults for missing or unsupported
settings. Automated opens never write sidecars. Signatures are unchanged; legacy
global RAW adjustments are ignored. See [camera-raw.md](camera-raw.md).
New defaults use Natural rendering and separate color-noise cleanup. Version 1
RAW sidecars retain their original neutral processing; version 2 stores the
profile and color-noise controls. Processing version 3 strengthens the Natural
default while preserving version 1 and 2 sidecars. Script signatures and API
version are unchanged.

2026-09-08 (API 1): Documents expose `getPalette`, `setPalette`, `loadPalette`,
and `savePalette` to scripts and MCP. Set/load preserve existing pixels and
enable palette-constrained editing and native indexed PNG export by default;
`enabled:false` keeps an inactive attached table. Native palette file I/O,
export order, duplicate colors, alpha threshold, and undo are supported.
Optional parallel `names` arrays preserve GPL color labels and travel through
PSD and indexed PNG. Palette/picker swatches expose Set Name/Rename; exact RGB
names appear in palette controls, color pickers and eyedropper readouts.
MCP discovery advertises `palettes`, `paletteColorNames`, and `indexedPng`.
See [palette-mode.md](palette-mode.md) and the scripting guide.

2026-09-08 (API 1): Attached MCP discovery survives an absent or closed Patchy.
Read tools retry attachment; `workspace_unavailable` reports absence and
`workspace_disconnected` reports an interrupted request with `retrySafe: false`.
Requests are never replayed. `get_info` includes `workspaceAvailable` and
reattachment requires a fresh state token. See [ai-control.md](ai-control.md).

2026-09-08 (API 1): `getShape().parts` exposes independent merged vector paints.
`mergeLayers` now retains different colors and strokes in one vector layer;
`separateVectorTypes` groups solid/gradient/pattern paint categories. Disabling
that option retains all appearances instead of inheriting the bottom paint.

2026-09-08 (API 1): `doc.mergeLayers(layers, options?)` adds the vector-preserving
Merge Layers planner without a dialog. Boolean options `keepVectors`,
`withinGroups`, and `separateVectorTypes` default to true, false, and true. It returns surviving
selected leaf layers in bottom-to-top order, validates before arming Undo, and
does not mutate the document for a no-op. Unlike `layer.merge_down`, a single
leaf does not implicitly include its lower sibling. `doc.combineShapes` retains
its existing boolean-operation semantics. See [layer-merging.md](layer-merging.md).

2026-09-08 (API 1): Successful unattended document opens and saves now update
shared recent files and folders. Owned MCP workspaces use the same persistent
history as the interactive application, honoring `PATCHY_SETTINGS_DIR` for tests.

2026-09-08 (API 1): Dynamic Vector Preview adds mixed-content compositing and a
Preferences checkbox. `view.vector_preview` and `view/vectorPreview` remain
unchanged. Routine preview status is tooltip-only; resource notices do not repeat
or replace existing status text. See [vector-preview.md](vector-preview.md).

2026-09-08 (API 1): `app.runCommand("view.vector_preview")` toggles the
persisted screen-resolution vector view. Window captures, including
`patchy.ui.captureWindow`, wait up to 60 seconds for its current render and return
false on timeout. Document previews, saved pixels and exports keep document
resolution. See [vector-preview.md](vector-preview.md).

2026-09-08 (API 1): `patchy.ui.paused` shares Pause/Resume for visible MCP and CLI
automation. Pausing finishes the current native edit; Resume appears when manual
editing is safe. Manual edits split script Undo groups. Targets resolve again
after resume; missing or incompatible targets raise an error. Browsing menus,
panels and informational dialogs remains available during work. Pause freezes
simulated painting time once parked and clears on completion or cancellation.
MCP advertises `pauseAutomation` and returns `paused` in state. The active request
remains busy; use the window's Resume button to continue it.

2026-09-07 (additive, API 1): `patchy.ui.slowMode` mirrors the Slow toggle beside
Stop. It presents each completed stroke/edit and gives it a separate Undo step,
within existing history limits. Defaults off; normal scripts retain grouped Undo.
It requires a visible workspace; headless runs reject it. It persists for the
workspace lifetime and appears in MCP state, with `slowModeAvailable`. Native timed
painting remains independent of display pacing.

2026-09-07 (additive, API 1): `patchy.brushes` discovers, resolves, previews,
creates/imports tips, saves independent complete presets, and explicitly activates
manual brushes. Native strokes add bitmap tips, full dynamics, Mixer Brush,
pen pose, smoothing, and simulated airbrush timing. Saved brushes also appear in
the UI. Resource writes persist outside document Undo. See [brush-automation.md](brush-automation.md).

2026-09-07 (additive, API 1): `patchy.ui.present(delayMs?)` presents completed
edits and optionally holds the frame for 0..1000 ms while servicing Stop.
Visible CLI/MCP runs repaint progressively; visible unattended scripts have a
status-bar Stop control. Image Size and `doc.resizeImage` compute a private resized
document while the existing Processing indicator remains responsive.

2026-09-07 (additive, still API 1): native `addShape`/`addFillLayer`,
`isShape`/`getShape`/`updateShape`/`transformShape`, `addGroup`/`groupLayers`/
`moveLayers`, saved/work/clipping path wrappers, vector-mask editing, path/selection
conversion, raster `fillPath`/`strokePath`, and `listVectorResources`. Fills and
outlines support all existing native paint types. MCP exposes vector state and
includes revisions and targets in attached tokens. See [vector-automation.md](vector-automation.md).
The installed skill remains a stable entry point to the connected app's types/examples.

September 2026 (additive, still API 1): MCP `--attach` connects to an existing
interactive workspace. Mutating MCP tools accept `expectedState` (required for
attachment); state/preview results include `stateToken`. State also exposes
session/history and layer render revisions. Connector restrictions and responsive
UI progress pumping apply only during connector-owned script runs. CLI and Finder
file opens wait for a running script to finish. See [ai-control.md](ai-control.md).

- **`app.apiVersion` is 1.** Bump it only for breaking API changes, and record what
  changed here. July 2026 additions (all additive, still 1): `include()` search roots,
  `patchy.isMainScript()`, `patchy.args`, `patchy.ui.showDialog`, `patchy.io.listFiles`,
  `app.chooseFolder/chooseOpenFile/chooseSaveFile`, `app.runCommand/commandIds`,
  `getPixels` reading 8-bit RGB layers (opaque opened photos) expanded to RGBA with
  alpha 255 (it previously threw; `setPixels` still always writes RGBA8 back),
  `patchy.ui.showOptions`, the `folder`/`file` form field types, the form dialogs'
  `description` header, `patchy.ui.playTone`/`patchy.ui.playSound`, and the UI staging
  quartet `patchy.ui.setWindowSize`/`setSidePanelWidth`/`captureWindow`/
  `setStatusMessage` (built for the README screenshot scripts in
  `scripts/dev/readme-shots/`; captureWindow rides the `--screenshot` grab machinery
  and never raises the window; setStatusMessage doubles as a progress readout). Behavioral fixes
  (still 1): `addTextLayer`'s `size` is defined as document pixels (it previously
  committed at a canvas-zoom-dependent size), and setting `activeLayer` reveals the
  row in the Layers panel (ancestor folders expand, the row scrolls into view).
  August 2026 additions (additive, still 1): the `patchy.filters.auto_tone` and
  `patchy.filters.auto_color` command ids reach `app.runCommand`/`commandIds` and
  `layer.applyFilter`, and `patchy.filters.auto_contrast` switched from per-channel
  to composite stretch (see filters.md; the id is unchanged). Later in August 2026:
  `image.auto_all` (Auto All) joined the registered command ids, and the three auto
  command ids now apply immediately with no settings dialog (behavioral; explicit
  `layer.applyFilter` invocations with an `amount` are unaffected). Also August 2026
  (additive, still 1): the `patchy.io` probes `fileExists`/`fileSize`/`makeDir`/
  `deleteFile`, added so a script can verify its own output (the AGENTS.md rule:
  missing test capabilities become scripting API); pinned by
  `ui_script_io_round_trips_unicode_path`. 2026-08-23 (additive, still 1):
  `layer.traceToShapes(options)` runs Trace Image to Shapes (docs/image-trace.md) on a
  pixel layer and returns the new group layer (null when nothing traced); the
  `layer.trace_image_to_shapes` command id reaches `app.runCommand` (it opens the dialog).
  2026-08-24 (still 1): `layer.traceToShapes` honors the document selection (behavioral);
  additive `layer.simplifyPath(options)`, `doc.combineShapes(layers, op)`, `layer.ungroup()`,
  and the command ids `path.simplify`, `layer.combine_*`, `layer.ungroup`, `edit.copy_svg`
  (docs/vector-commands.md). 2026-08-25 (additive, still 1): `layer.traceToShapes`
  accepts `smoothing` (0..10 px pre-quantization denoise) and `maxAnchors` (anchor
  budget, 0 = unlimited), and `colors` extends to 2..256 (values above 64 previously
  clamped to 64; docs/image-trace.md). Also 2026-08-25 (behavioral plus additive,
  still 1): with a document selection `layer.traceToShapes` picks its palette from
  the whole layer, matching a whole-layer trace's colors;
  `paletteFromLayer: false` restores selection-scoped colors, and the additive
  `mergeColors` option (0..100, default 0) merges near-duplicate palette entries
  (docs/image-trace.md).

September 2026 additions (additive, still 1): document and layer `id`,
`app.getDocument`, `doc.getLayer`, `doc.modified`, `doc.canUndo`, `doc.canRedo`,
`doc.undo`, `doc.redo`, `doc.renderPreview`, `layer.drawStrokes`, and
`patchy.setResult`. The native MCP connector uses the same API. Identifiers and
semantics are specified in [ai-control.md](ai-control.md) and the packaged
`patchy.d.ts`; menu commands and interactive canvases are unavailable in connector
sessions. Ordinary scripts retain their existing interactive behavior.

2026-09-06 (behavioral, still 1): `layer.fillRect`, `selection.selectRect`, and
`selection.selectEllipse` throw for a side over 30000 (the document limit) instead of
sizing a buffer or region from the raw argument, which ended in a bad_alloc no JS catch
can see; `layer.opacity` refuses NaN; `patchy.io.readTextFile` throws for files over
256 MB; `doc.activeLayer` refuses a layer wrapper from another document (LayerIds
restart per document, so it activated an unrelated layer before); and text layers whose
characters no registered font covers no longer crash the missing-font check (Thai and
Japanese under `--headless`, where only bundled and rescued faces exist).

September 6, 2026 behavioral corrections (API version remains 1): forwarded
unattended scripts suppress file/close prompts and use default RAW/PDF imports;
forms normalize defaults through the interactive controls and reject missing keys.
`app.runCommand` refuses `edit.undo`, `edit.redo`, and `file.quit` during a run.
RGB8 layers support `fill`/`fillRect`, positions reject overflow, selections clip to
the canvas, and assigning empty text clears its raster. Existing identifiers and
Qt color/button encodings remain unchanged.

2026-09-06 (additive, still 1): `patchy.ui.zoom` (read/write percent of the active
document's view, 0 with no document, clamped to 5..12800, throws for NaN or
non-positive values or with no document) and `patchy.ui.fitOnScreen()`. They work in
connector sessions, where `app.runCommand('view.fit_on_screen')` is refused, and only
affect window captures, never document previews. Pinned by `ui_script_ui_view_zoom` and
the connector run in `tests/mcp_client_tests.py`.

2026-09-21 (additive, still 1): `PatchyShapeState.feather` (px) and `.density` (0..100),
plus `layer.updateShape({feather, density})`: Photoshop's vector-mask Feather / Density on a
shape layer's own path, the same convention as `setVectorMask`. Pinned by
`ui_script_shape_feather_and_density`.

2026-09-22 (additive, still 1): `app.exportPdf` options gain `imageQuality` (`"lossless"`,
`"high"`, `"medium"`, `"low"`; an unknown id throws), which wins over `lossless`. Image
pages now go through Patchy's own PDF writer: `lossless: false` means JPEG quality 90
(it was Qt's fixed 94), and gray pages are written as one channel in every mode. The
default stays lossless. `keepOriginalImageData` (default true) writes a page that was
imported from a PDF as one image, and has not visibly changed since, with that image's
original bytes. Pinned by `ui_script_export_pdf_writes_pages`.

2026-10-01 behavioral correction (API version remains 1): `doc.resizeImage` re-renders
every editable embedded and every resolvable linked smart object from its source
(vector files at the new scale), matching Image > Image Size; before, the script and
MCP resize kept the resampled previews, and linked placements stayed resampled in
every path. A linked file that is missing or cannot be decoded keeps the resampled
preview without throwing, and `getSmartObject().missing` still reports it. Pinned by
`ui_script_smart_object_image_size_rerenders_linked_and_embedded`,
`ui_script_smart_object_linked_raster_rerenders_from_full_resolution` and
`ui_script_smart_object_missing_linked_file_keeps_preview_on_image_size`.
