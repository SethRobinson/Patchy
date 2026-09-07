---
name: patchy-control
description: Create and edit layered artwork or pixel art in Patchy using its local MCP connector or headless JavaScript API. Use for painting, image edits, and editable art deliverables in Patchy.
---

# Control Patchy

Use Patchy's native editing API. The local MCP connector owns an isolated background workspace and does not attach to an artist's open window.

## Connect and discover

Use the configured Patchy tools. Call `get_info` and `get_state`, then read `get_help` with `topic: "api"` before writing scripts. The installed [API reference](references/patchy.d.ts) contains the same definitions. Read the [scripting guide](references/scripting-guide.md) for broader operations.

If the connector is not configured, use the package's [setup instructions](references/setup.md). Installing this skill does not register an MCP server. A shell-capable agent can also use the headless CLI below.

## Edit, inspect, revise

- Use document and layer IDs returned by state, not assumptions about the active tab or unique layer names. IDs are decimal strings. Re-query after undo, deletion, or reopening.
- `execute_script` runs ES6-level JavaScript in Patchy, not Node or a browser. Use `app.getDocument(id)`, `doc.getLayer(id)`, and the documented `patchy.*` API. Globals reset between requests; documents persist while the connection stays open.
- Batch related edits into one script or `draw_strokes` request. Each mutating run gives one undo entry per document. Do not issue concurrent requests or retry a mutation automatically after an uncertain response.
- Use `layer.drawStrokes` for real Brush/Eraser paths and pressure. Coordinates are document pixels. Set color, size, Flow, opacity, softness, and seed explicitly when their exact behavior matters. Inspect the API's supported fields rather than inventing brush settings.
- Pressure scales opacity as well as size, so a stroke tapered with low pressure also fades. For a solid tapered shape such as an ear or a tail, keep pressure at full strength and vary `size` across several strokes, or fill the shape with short parallel strokes. Use pressure when a fading, thinning line is the intent.
- For exact pixel art, write palette-colored RGBA arrays with `setPixels`, or use `fillRect`. `setPixels` replaces the layer buffer; it does not update a subregion. Use separate layers for independently editable objects.
- Call `get_preview` after a meaningful batch, inspect the image, then refine. Crop details using document coordinates. For small sprites use `nearestNeighbor: true` and bounded dimensions. The returned rectangle and scales map preview pixels to the document.
- Use `get_preview` with `target: "window"` only when the app layout matters; this is an offscreen app render. Canvas previews are better for assessing artwork. Stage the view first with `patchy.ui.fitOnScreen()` or `patchy.ui.zoom = <percent>` in a script; menu commands are unavailable.
- Return small structured values with `patchy.setResult({...})`. Logs are separate. A script completes after its timers finish; avoid unbounded intervals. Long pure-JS computations need occasional progress logs to feed the inactivity watchdog.
- On failure, inspect the error and updated state. Partial edits may remain and can be undone. `undo`/`redo` restore one history step; in scripts call them before any new edits.
- Keep undo enabled. Connector sessions reject `app.undoEnabled = false`.

## Save and deliver

Save checkpoints before substantial revisions and final layered artwork with `doc.saveAs(path)`. Check its boolean result. Write a PNG with `doc.renderPreview(path, options)` when its bounded output size is appropriate; this preserves the PSD path and modified state. For full-resolution format export use `doc.exportAs(path)`, which currently has the same save-path behavior as `saveAs`; save the PSD last if both are used.

Return the editable file and preview paths. Documents and undo history disappear when the connector exits; an open connection is not a saved checkpoint. Scripts have the application's file privileges and should access only task-relevant files. The connector does not provide a filesystem sandbox or a live-window control permission.

## Examples and CLI

- [Layered pixel art](scripts/pixel-art.js): creates a sprite from palette rows.
- [Pressure painting](scripts/painting.js): creates layered native strokes.
- [Edit a document](scripts/edit-document.js): opens an input and saves a separate output.

With a shell, run the package's executable using an absolute path:

```powershell
& "$env:LOCALAPPDATA\Programs\Patchy\patchy.exe" --headless --run-script 'job.js' --script-output 'job-result.txt'
```

Pass script parameters as repeated `--script-arg key=value`. Each headless launch is a fresh workspace and exits on completion. Check the process exit code and the output's final `[done]` or `[failed]` marker. Use unique output paths. Without `--headless`, the command can forward to an existing artist window; use headless for background work.

Use `doc.renderPreview` for intermediate PNGs and `patchy.ui.captureWindow` for offscreen app captures. Inspect those files with the agent's image-viewing tool. Save and reopen a checkpoint between separate CLI runs.
