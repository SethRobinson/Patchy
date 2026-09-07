# Control Patchy

Use Patchy's native editing API. Check `get_info` before editing: `workspace: "attached"` means the user's already-open Patchy workspace, including unsaved documents; `workspace: "isolated"` means the connector's own workspace. `--attach` selects the first, `--visible` opens a separate visible window, and no argument creates a hidden workspace. Never substitute a new workspace when the user asks to edit their open document. Check actual mode and visibility, not just startup arguments.

## Connect and discover

Use the configured Patchy tools. Call `get_info` and `get_state`, then read `get_help` with `topic: "api"` before writing scripts. Fetch `topic: "guide"` for broader operations. The packaged [API reference](patchy.d.ts) and [scripting guide](scripting-guide.md) contain the same definitions for CLI clients.

This workflow is served from the connected Patchy installation. The assistant's installed skill is only an entry point; old references copied into a client skill folder are not current documentation. After a Patchy upgrade, save work, reconnect, and fetch the workflow and API again.

If the connector is not configured, use the package's [setup instructions](setup.md). Installing this skill does not register an MCP server. A shell-capable agent can also use the headless CLI below.

For art from a supplied photo or image, read [Reference artwork](reference-art.md), also available as `get_help` with `topic: "reference-art"`. Use it to plan the crop, palette, editable layers, and preview comparisons. The setup guide includes example user requests for icons, sprite processing, and PSD edits.

## Choose the workspace for the task

Setup is done once. The example prompts in Help > Set up AI Control describe tasks; choosing one does not change or reinstall the connector or skill. Preserve a working installation when the user changes between background work and their open document.

Use the configured MCP connection when `get_info` matches the requested workspace. MCP startup mode is fixed for that connection; a prompt does not turn an attached window into a hidden one. With shell access, use the same installation's command-line API for a task that needs another workspace:

- For background work, use `patchy --headless --run-script ... --script-output ...`. It creates a private offscreen workspace. Save a PSD checkpoint between calls and read preview PNGs from disk. It never touches the user's open tabs.
- For the user's open document, prefer attached MCP. Otherwise verify that the matching Patchy application is already running, then use `patchy --run-script ... --script-output ...` with no positional image paths. First inspect `app.activeDocument` and `app.documents`, log their IDs, and save a preview with `doc.renderPreview`. Recheck the intended IDs in the editing script, keep Undo enabled, and inspect the result. Poll the output file for `[done]` or `[failed]`; the forwarding process exits before the script finishes. If the application is unavailable, ask the user to open it; never create a replacement and claim it is their document.
- For visible creation, use an available visible MCP workspace or run a non-headless CLI script. That command can forward into an existing window, so create a new document for the task and preserve existing documents. Without a running window, the CLI window lasts for that script; save results before it exits. Only show a window when the user's request authorizes it.

CLI runs use the existing script-progress UI, not the MCP connection indicator. Read the API and workflow from this installation if its configured MCP connection is unavailable. A client with only MCP tools and no shell cannot change workspaces within the connection. Explain that limit if it matters and offer a connection-mode change with reconnection, not reinstalling the skill or application. Do not change client configuration just because the user selected a different example.

## Edit, inspect, revise

For painterly artwork, fetch `get_help` with `topic: "painting-guide"`. Discover
tips/presets through `patchy.brushes`, inspect native swatches, and choose settings
before committing a large painting batch. Full Brush dynamics, Mixer pickup, Wet
Edges, pen pose, smoothing and timed airbrush are available; an "oil" label alone
does not choose them. Fetch `brush-swatches`, `wet-paint`, `brush-library`, or
`timed-brush` examples. Ordinary strokes restore the artist's settings.

- When the user wants to watch, draw incrementally into the real document and
  call `patchy.ui.present(60)` after each stroke, shape, or small pixel-art step.
  This shows the frame and briefly holds it while Stop remains usable. Use
  `present()` without a hold when pacing is unnecessary. For buffered pixel art,
  upload the current full buffer with `setPixels` at intermediate steps before
  presenting; private arrays are invisible until uploaded. The vector-art example
  accepts `watch=true`. Hidden work should omit deliberate pacing.

- In attached mode, first inspect `get_state` and `get_preview`. Pass the latest returned `stateToken` as `expectedState` to every `execute_script`, `draw_strokes`, `undo`, and `redo` request. Arbitrary scripts need the token even if you intend only to read. A `stale_state` error means no edit ran: inspect the new state and preview, reconsider the edit, and only then retry with the fresh token. Switching tabs or editing pixels invalidates an older view. The token is opaque and valid only for that connection.
- To fix something in an open document, identify its ID, inspect the face or other relevant region, and prefer a separate correction layer. Leave the document open and preserve unrelated layers. Do not create a replacement document unless requested. Use document and layer IDs in the script even when the intended tab is currently active.
- Patchy's status bar distinguishes AI connected, AI reading, and AI editing. Connected means waiting for a tool call, not that the model has finished thinking. Editing is paused during a request; Stop cancels the current operation and leaves available undo history. Long scripts should call APIs or log progress periodically so the visible window and Stop control can respond. Pure JavaScript with no API calls cannot pump the UI; client cancellation and the inactivity watchdog still interrupt it.
- Use document and layer IDs returned by state, not assumptions about the active tab or unique layer names. IDs are decimal strings. Re-query after undo, deletion, or reopening.
- `execute_script` runs ES6-level JavaScript in Patchy, not Node or a browser. Use `app.getDocument(id)`, `doc.getLayer(id)`, and the documented `patchy.*` API. Globals reset between requests; documents persist while the connection stays open.
- Batch related edits into one script or `draw_strokes` request. Each mutating run gives one undo entry per document. Do not issue concurrent requests or retry a mutation automatically after an uncertain response.
- Use `layer.drawStrokes` for real Brush/Eraser paths and pressure. Coordinates are document pixels. Set color, size, Flow, opacity, softness, and seed explicitly when their exact behavior matters. Inspect the API's supported fields rather than inventing brush settings.
- Default pressure scales opacity and size. For a painted taper that stays opaque,
  use independent dynamics controls: `sizeControl: 'penPressure'`,
  `opacityControl: 'off'`. For editable vector contours, use native shape paths.
- For editable vector artwork, use `doc.addShape`, `layer.getShape`/`updateShape`, and `transformShape`. Geometry and appearance are independent; do not substitute raster strokes or SVG imports for requested native shape creation. Inspect native shape/editability flags first when revising existing artwork. Fetch `vector-art` or `edit-shape` examples and the current API. Shape defaults are black fill and no outline, independent of toolbar settings. Paints support solid, gradient, pattern, and none; discover resource IDs with `doc.listVectorResources()`.
- Path/anchor values are detached document-pixel snapshots. Commit changes explicitly and refresh group/anchor references after replacement or Undo. Saved/work paths, vector masks, path/selection conversion, and raster fill/stroke along paths share that representation; fetch the `paths-masks` example. Shapes already use their native vector-path slot, so put them in a group and apply additional vector masks to that group. Empty shape geometry is invalid; use `addFillLayer` for full-canvas paint. Empty vector masks reveal all unless inverted; remove them explicitly.
- For exact pixel art, write palette-colored RGBA arrays with `setPixels`, or use `fillRect`. `setPixels` replaces the layer buffer; it does not update a subregion. Use separate layers for independently editable objects.
- Call `get_preview` after a meaningful batch, inspect the image, then refine. Crop details using document coordinates. For small sprites use `nearestNeighbor: true` and bounded dimensions. The returned rectangle and scales map preview pixels to the document.
- Use `get_preview` with `target: "window"` only when the app layout matters; it captures the connected window, with actual `offscreen` metadata. Canvas previews are better for assessing artwork. Stage the view only when requested using `patchy.ui.fitOnScreen()` or `patchy.ui.zoom = <percent>` in a script; menu commands are unavailable. Visible work updates between batches, timer callbacks, or progress pumps. A `busy` reply can mean the user has an unfinished gesture, text edit, transform, crop, dialog, or local script. Let that finish before editing.
- Return small structured values with `patchy.setResult({...})`. Logs are separate. A script completes after its timers finish; avoid unbounded intervals. Long pure-JS computations need occasional progress logs to feed the inactivity watchdog.
- On failure, inspect the error and updated state. Partial edits may remain and can be undone. `undo`/`redo` restore one history step; in scripts call them before any new edits.
- Keep undo enabled. Connector sessions reject `app.undoEnabled = false`.

## Save and deliver

Save checkpoints before substantial revisions and final layered artwork with `doc.saveAs(path)`. Check its boolean result. Write a PNG with `doc.renderPreview(path, options)` when its bounded output size is appropriate; this preserves the PSD path and modified state. For full-resolution format export use `doc.exportAs(path)`, which currently has the same save-path behavior as `saveAs`; save the PSD last if both are used.

Return the editable file and preview paths. For exact-size deliverables specify both `maxWidth` and `maxHeight`; a 512x512 enlarged preview is not a 64x64 export. Isolated documents and undo history disappear when the connector exits. An attached disconnect leaves the user's documents open and never saves or closes them automatically. An open document is not a saved checkpoint. Save before upgrades or switching out of an isolated workspace; reconnect and re-query IDs and state. Scripts have the application's file privileges and should access only task-relevant files. The connector does not provide a filesystem sandbox or permission to control other applications.

## Examples and CLI

- [Layered pixel art](../scripts/pixel-art.js): creates a sprite from palette rows.
- [Pressure painting](../scripts/painting.js): creates layered native strokes.
- [Edit a document](../scripts/edit-document.js): opens an input and saves a separate output.
- [Native vector artwork](../scripts/vector-art.js): creates a ginger cat in stages, revises a curve and appearance, then saves PSD/SVG/PNG. Preview after every stage.
- [Edit a shape](../scripts/edit-shape.js): inspects a chosen native shape, revises its appearance, and saves a reviewed checkpoint.
- [Paths and masks](../scripts/paths-masks.js): shares an inspected outline with a saved path, mask, selection, and fitted work path.

With a shell, run the package's executable using an absolute path:

```powershell
& "$env:LOCALAPPDATA\Programs\Patchy\patchy.exe" --headless --run-script 'job.js' --script-output 'job-result.txt'
```

Pass script parameters as repeated `--script-arg key=value`. Each headless launch is a fresh workspace and exits on completion. Check the process exit code and the output's final `[done]` or `[failed]` marker. Use unique output paths. Without `--headless`, the command can forward to an existing artist window; use headless for background work.

Use `doc.renderPreview` for intermediate PNGs and `patchy.ui.captureWindow` for offscreen app captures. Inspect those files with the agent's image-viewing tool. Save and reopen a checkpoint between separate CLI runs.
