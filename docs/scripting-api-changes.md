# Scripting API compatibility

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
