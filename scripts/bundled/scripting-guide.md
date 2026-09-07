# Patchy Scripting Guide

Patchy has a built-in JavaScript engine that can automate almost anything you can do by hand: edit layers and pixels, run filters, add text, save and export, batch-process whole folders, and even open small game windows. Scripts are plain `.js` files (modern ES6-level JavaScript), and this guide covers everything you need to write them.

For exact type signatures, see `patchy.d.ts` in the same folder as this guide. It is the machine-readable version of this document, and editors like VS Code use it for autocomplete.

## Running scripts

There are four ways to run a script:

1. **The File > Scripts menu.** Every bundled script and every script in your user scripts folder shows up here, organized by folder.
2. **The Script Manager** (File > Scripts > Script Manager). A folder tree, a code editor with syntax highlighting, a console, and Run/Stop buttons. Click a script in the tree to see its code (unsaved edits stay put until you save or confirm switching away), and press **F5** to run what is in the editor. This is the best place to write and test scripts.
3. **The command line.** `patchy --run-script myscript.js` runs a script unattended, for batch jobs and external tools. See the Command line section below. The Script Manager's **C:\\** toolbar button shows a ready-made command line for any script.

4. **The local MCP connector.** A desktop package includes `patchy-mcp` and the installable `ai/patchy-control` skill (inside Resources on macOS, or share/patchy on Linux). Configure the connector as a stdio MCP server. `get_help` provides this guide, the API reference, the workflow, and examples. No Python or Node installation is required. `--attach` connects to your already-open
   Patchy workspace, `--visible` opens a separate visible window, and no argument
   creates a hidden workspace. Attached edits require `expectedState` from the
   latest `get_state` or preview. The status bar shows AI connected, reading, or
   editing, with Stop during an edit. Disconnecting an attached client leaves
   your unsaved documents open. Read `get_help(workflow)` for the full procedure.

A script run normally creates **one undo entry per document**. Enable **Slow** beside Stop to watch each native stroke or undoable edit with a short pause and a separate Undo step. You can toggle it during work. The existing history limits still apply. `patchy.ui.slowMode` reads or changes the same workspace setting; it defaults off and stays selected between requests until you close Patchy. Headless runs reject Slow mode and keep normal speed and grouped Undo. Slow mode does not change timed airbrush or smoothing output.

## Your first script

Open the Script Manager, paste this into the editor, and press F5 with a document open (the **New** button, also offered when you right-click a folder like My Scripts, starts you with a similar ready-to-run template):

```js
var doc = app.activeDocument;
if (!doc) {
  app.alert("Open a document first.");
} else {
  var layer = doc.addLayer("Red box");
  layer.fillRect(20, 20, 200, 120, "#e04040");
  doc.addTextLayer("Hello from a script", { x: 30, y: 60, size: 32, color: "#ffffff" });
  console.log("Done: " + doc.width + "x" + doc.height);
}
```

`app` is the application, documents hold layers, and `console.log` writes to the Script Manager's console pane. That is most of the model already.

Save it with the Save button and it lands in your user scripts folder, which means it also appears in the File > Scripts menu.

## Persistent agent workspaces

Each `patchy-mcp` connection owns an offscreen workspace. Documents and undo history stay open across requests; JavaScript globals reset for every script. Save files explicitly before disconnecting. The connector isolates settings and disables single-instance forwarding, sound, and update checks.

Call `get_state` to inspect document/layer IDs, hierarchy, dimensions, selection, modified state, and history availability. IDs are decimal strings: document IDs last until close, and layer IDs identify a layer within its document while it exists. Undo can remove or restore layers. Re-query state after history changes and never keep IDs across connector restarts or document reopen. Use lookups in later requests:

```js
var doc = app.getDocument("1"); // use the ID from get_state
var layer = doc.getLayer("2");
layer.drawStrokes([{
  tool: "brush", color: "#397ac9", size: 18, opacity: 85,
  flow: 35, softness: 60, seed: 42,
  points: [{x: 20, y: 40, pressure: 0.2},
           {x: 80, y: 25, pressure: 1},
           {x: 140, y: 55, pressure: 0.4}]
}]);
patchy.setResult({documentId: doc.id, layerId: layer.id});
```

`drawStrokes` uses native Brush, Eraser, or Mixer strokes, including bitmap tips, complete Brush dynamics, Wet Edges, pressure/tilt/rotation, spacing, smoothing, Flow accumulation, selections, alpha locks, and palette snapping. It temporarily sets its own brush settings and restores the artist's settings. It requires an unlocked 8-bit RGB/RGBA pixel layer; text, vectors, Smart Objects, and groups must first be converted or painted on a separate pixel layer. Point coordinates are document pixels. Missing pressure means ordinary full-pressure mouse behavior; supplied pressure uses the default pen size/opacity mapping (20% size floor, 15% opacity floor). Size-one paths use exact pixel segments. Timed airbrush requires complete nondecreasing `timeMs` samples starting at zero. Smoothing and airbrush use simulated time, independent of `present()` delays. Use `patchy.brushes.listTips()`, `listPresets()`, and `resolve()` before painting; `renderPreview()` saves native swatches without document edits. `getCurrent()` captures the artist's brush explicitly, while `activate()` deliberately changes it. `createTip`, `importAbr`, and saved-preset operations persist resources outside document Undo. Saved presets snapshot their tip and appear in the UI. Mixer uses its native full opacity and Flow; ordinary Brush dynamics do not apply to Mixer/Eraser. Read `PatchyStroke` in `patchy.d.ts` for defaults, fields, and limits. Unknown fields and malformed batches fail before painting. For exact sprites, use palette-colored RGBA arrays with `setPixels` or rectangles with `fillRect`.

Inspect with `get_preview`: it returns PNG image content plus the crop rectangle and X/Y scale. The JavaScript equivalent writes a PNG without changing the document's save path or modified state:

```js
var preview = doc.renderPreview(patchy.args.preview, {
  rect: {x: 0, y: 0, width: 32, height: 32},
  maxWidth: 256, maxHeight: 256, nearestNeighbor: true
});
patchy.setResult(preview);
```

Preview dimensions default to a 1024 by 1024 bounding box and may be 1 through 4096. The aspect ratio is preserved. Ordinary previews shrink as needed; nearest-neighbor previews may enlarge pixel art. Crop rectangles are clipped to the canvas. For full-resolution outputs beyond the preview bound, use `exportAs`; it currently behaves like `saveAs`, so save the layered PSD last. MCP `get_preview` with `target: "window"` is an offscreen app-window render for inspecting the interface.

A mutating script or stroke batch normally makes one undo entry per affected document; Slow mode separates strokes and edits within the batch. Failed or cancelled scripts may leave partial edits; inspect returned state and undo before revising. The connector does not retry edits. `doc.undo()` and `doc.redo()` return whether a history step was restored and must run before new edits in the same script. `doc.modified`, `doc.canUndo`, and `doc.canRedo` expose status. `patchy.setResult(value)` returns a small JSON value independently of logs.

Send one tool request at a time. A concurrent edit/state request receives `busy`. Cancellation interrupts JavaScript, stops timers, and lets native work reach an interruption boundary. The inactivity watchdog still applies. `app.runCommand` and `patchy.ui.createCanvas` report unsupported operations in connector sessions; use explicit document APIs (`patchy.ui.zoom` and `patchy.ui.fitOnScreen()` set the view before a window capture). Existing unattended option/dialog behavior applies. Scripts retain Patchy's trusted-script file privileges.

## Native shapes, paths, and masks

Use native shapes for editable illustrations, icons, diagrams, and reusable
outlines. Inspect `layer.isShape` and `layer.getShape()` before editing an existing
layer. Imported content can have `editable:false`; keep those layers intact.
`patchy.d.ts` defines every option, default, unit, and returned snapshot.

```js
var doc = app.newDocument(960, 960);
doc.addFillLayer('Backdrop', '#d9ebe4');
var face = doc.addShape('Face',
  {type:'ellipse', x:240, y:240, width:480, height:380},
  {fill:'#efaa60', stroke:{width:6, paint:'#733f32'}});
patchy.setResult({documentId:doc.id, layerId:face.id, shape:face.getShape()});
```

New shapes default to black fill and no outline, independently of toolbar settings.
Supplying a stroke enables it by default with inside alignment. Geometry types are
rectangle, roundedRectangle (one radius or TL/TR/BR/BL radii), ellipse, line (weight
and arrowheads), polygon (sides and optional starInset), custom (resourceId and
bounds), and path. Full-canvas fills use `addFillLayer`; empty shape geometry fails.

Preview after this batch. In the next script, resolve those document/layer IDs and
revise a small part. Globals reset between MCP calls, while documents persist.

```js
var doc = app.getDocument(patchy.args.documentId);
var face = doc.getLayer(patchy.args.layerId);
face.updateShape({fill:'#f2af69', stroke:{width:4}});
var path = face.getShape().path; // detached, changing this does nothing by itself
path.subpaths[0].anchors[0].outY -= 12;
face.updateShape({path:path});  // explicitly commit the changed curve
```

Appearance updates preserve live geometry. Direct path edits invalidate only
changed live groups. `updateShape({geometry:...})` replaces geometry; add `group`
to replace one inspected group. Geometry and path replacement cannot be combined.
Refresh anchor/group references after geometry replacement or Undo.

All path coordinates and handles are absolute document pixels:

```js
var outline = {subpaths:[{closed:false, operation:'unite', group:0, anchors:[
  {x:10,y:30,outX:20,outY:5,smooth:true},
  {x:70,y:30,inX:60,inY:5,smooth:true}
]}]};
```

Omitted handles coincide with the anchor. `closed` defaults true; operations are
unite/subtract/intersect/exclude. Same-group subpaths use native even-odd fill,
then group operations combine coverage. Paths allow 4096 subpaths and 100000
anchors total; each subpath needs two anchors. Coordinates must be finite and in
-100000..100000. Open path fills use an implied closing chord; strokes do not.

`layer.transformShape([a,b,c,d,tx,ty],{strokeScale:1})` uses native affine geometry:
`x'=a*x+c*y+tx`, `y'=b*x+d*y+ty`. It also accepts groups containing shapes and
nested groups, validating every child first. Mixed groups and singular transforms
fail. Vector masks transform with this native geometry; raster mask pixels keep
their own coordinates. Stroke width stays fixed unless strokeScale is supplied.
Supported transforms retain live parameters; other transforms keep editable paths.

`doc.addGroup(name)`, `doc.groupLayers(layers,name)`, and
`doc.moveLayers(layers,{parentId:group.id,index:0})` organize artwork. Grouping
requires siblings and keeps their order. Destination index counts bottom to top
after removing moving layers; null/omitted parentId means root, omitted index
means top. Existing duplication, `ungroup`, and `combineShapes` also work.

For a visible demonstration, call `patchy.ui.present(60)` after each shape or
stroke to show the step and hold it briefly. The default `present()` just presents
completed edits; it does not add history. Delays are integers from 0 to 1000 ms.
Ordinary visible MCP/CLI edits also refresh periodically without explicit calls.
Write changed JavaScript arrays back with `setPixels` before presenting them.
Use no deliberate delay for background work. Stop remains responsive during the
hold; script timer callbacks cannot reenter the current script.

Fills and stroke paints accept `"none"`, RGB color strings, or typed solid,
gradient, and pattern objects. Solid opacity belongs to the layer or stroke.
Stroke fields include width, alignment, cap, join, miter limit, dashes/dashOffset
(width multiples), opacity (0..1), blendMode, scaleLock, adjust, and fillEnabled.
Partial updates preserve omitted values; explicitly enable a previously disabled
stroke with `enabled:true`.

```js
face.updateShape({fill:{type:'gradient',gradient:{type:'radial',scale:1,
  colorStops:[{position:0,color:'#fff0ce'},{position:1,color:'#efaa60'}],
  alphaStops:[{position:0,opacity:1},{position:1,opacity:1}]}}});
var resources = doc.listVectorResources();
var pattern = resources.patterns.filter(function(p){return p.source==='library';})[0];
if (pattern) { face.updateShape({fill:{type:'pattern',source:pattern.source,
  resourceId:pattern.resourceId,scale:0.5}}); }
```

Gradients expose their current definition and placement: linear/radial/angle/
reflected/diamond, solid/noise, stop midpoint/alpha, native smoothness, interpolation,
reverse/dither, angle/scale, alignment/offset, and noise ranges/seed. A gradient
`presetId` replaces its definition while retaining omitted placement. Foreground
and background inputs default black/white; supply them explicitly if needed.
Do not mix presetId with explicit definition fields in one update. Numeric paint
scales use 1 = 100%; gradient offsets use native percentages. Pattern references
distinguish library storage IDs and document IDs. Library application adopts the
actual resource, handling ID collisions; inspection returns the adopted document
reference. Resource enumeration is read-only and does not manage the libraries.

Document paths use decimal-string IDs and wrappers that resolve through their
owning document. `doc.paths` lists saved/work paths; `workPath` and `clippingPath`
return a wrapper or null. `getPath(id)`, `addPath(name,data)`, and `setWorkPath(data)`
provide targeted access. Path wrappers offer name, kind, getPath/setPath,
transform, duplicate, remove, moveTo, activate, and save. `work.save(name)` makes a
saved path at the end, retaining its ID; `moveTo(index)` orders saved paths only.
Clipping assignment accepts one saved path from the same document or null.

```js
var saved = doc.addPath('Face outline', face.getShape().path);
doc.clippingPath = saved;
var masked = doc.groupLayers([face], 'Masked face');
masked.setVectorMask({path:saved.getPath(),enabled:true,inverted:false,
  linked:true,density:100,feather:0});
doc.selection.fromPath(saved.getPath(),{operation:'replace',antialias:true,feather:0});
doc.setWorkPath(doc.selection.toPath({tolerance:1}));
```

Shapes already use their native vector-path slot for geometry. Apply additional
vector masks to groups containing those shapes. Ordinary pixel layers can carry
a vector mask directly. The API rejects adding a second path slot to a shape.
Mask density is 0..100 and feather is 0..1000 pixels. `getVectorMask()` returns a
snapshot or null; `setVectorMask` creates or partially updates it. An empty path
reveals all unless inverted. Use `removeVectorMask()` to remove one,
`transformVectorMask(matrix)` to move only its geometry, or
`rasterizeVectorMask()` to bake it through the native operation.
Selection operations include replace/add/subtract/intersect. `toPath` fits the
existing selection outline and returns geometry without storing it. Quick Mask
must be exited before path-to-selection conversion.

To paint on pixels, explicitly target an unlocked 8-bit pixel layer and call
`layer.fillPath(path,{paint:'#ffaa77',opacity:1})` or
`layer.strokePath(path,{color:'#733f32',size:4,seed:0})`. Fill accepts all native
paints, and stroke uses the documented native Brush/Eraser options plus pressure.
Both honor the selection and palette behavior. Native editable outlines instead
belong to the shape's stroke appearance.

Invalid operations are validated before mutation and do not add history. Earlier
edits in a script that later fails remain undoable under its one-entry history
contract. In attached MCP, pass expectedState from the latest state/preview even
to an inspection script. State includes compact vector flags, masks, path IDs and
revisions, and the active vector target; fetch full geometry with a targeted call.
After a batch, inspect a fresh preview, revise, and save an editable PSD checkpoint.
SVG exports supported native artwork; PSD retains richer layers/masks/paths.
If exporting multiple formats, save PSD last to retain its document path.

Served examples: `get_help` topics `vector-art` (staged ginger cat), `edit-shape`,
and `paths-masks`. Each demonstrates small batches with preview/review between runs.

## Header directives

A comment block at the top of a script describes it to the Script Manager and the Scripts menu:

```js
// @name Batch Export
// @description Converts a whole folder of images: opens every file matching
// @description a pattern and exports it in the chosen format.
// @author Jane Doe
// @cli --script-arg folder=C:\photos --script-arg out=C:\photos\converted
```

| Directive | Meaning |
| --- | --- |
| `@name` | Display name shown instead of the file name. |
| `@description` | Hover-card blurb. Repeat the line to continue the text. |
| `@author` | Credit line on the hover card. |
| `@window` | The script creates its own window or document (shown as a window badge). Scripts without it are expected to work on the active document. |
| `@cli` | The argument part of the script's command-line example: everything after `--run-script <script>`. Repeat the line to continue it. Shown by the Script Manager's **C:\\** button; without it the example falls back to a plain `example.png` placeholder. |

A 128x128 PNG next to the script with the same base name (`myscript.js` and `myscript.png`) becomes its icon. Right-click a script in the Script Manager for **Set Icon from Current Window**.

## Script options

The bundled scripts share one pattern for tweakable settings, and it is worth copying:

```js
// Defaults live in one clearly-marked block at the top of the file.
var OPTIONS = {
  size: 4,          // dot size in pixels
  invert: false
};

var options = patchy.ui.showOptions({
  title: "Halftone",
  description: "Turns the active layer into a dot pattern.",
  fields: [
    { key: "size", label: "Dot size", type: "slider", value: OPTIONS.size, min: 1, max: 32 },
    { key: "invert", label: "Invert", type: "checkbox", value: OPTIONS.invert }
  ]
});
if (options) {
  // options.size, options.invert hold the effective values.
}
```

`showOptions` does three jobs in one call:

- **GUI runs** show a dialog seeded with the defaults. `null` means the user cancelled, so exit quietly.
- **Command-line runs** skip the dialog and return the defaults.
- `--script-arg key=value` **overrides a field** in both cases (values are coerced to the field's type; a bare `--script-arg flag` turns a checkbox on).

Field types: `number`, `slider`, `checkbox`, `choice`, `text`, `color`, `folder`, and `file`. The `folder` and `file` types render as a path box with a Browse button. See `PatchyDialogField` in `patchy.d.ts` for every property.

## API reference

### Globals

| Global | What it is |
| --- | --- |
| `app` | The application: documents, dialogs, commands. |
| `patchy` | The namespace: `patchy.ui`, `patchy.io`, `patchy.args`, `patchy.isMainScript()`, `patchy.version`, `patchy.apiVersion`. |
| `console` | `log`, `info`, `warn`, `error`; output goes to the Script Manager console and to `--script-output`. |
| `setTimeout` / `setInterval` / `clearTimeout` / `clearInterval` / `requestAnimationFrame` | Timers, like in a browser. The run stays alive while timers are pending. |
| `include(path)` | Runs another script file in the same scope. Relative paths resolve against the running script, then your user scripts folder, then the bundled scripts, so `include("Effects/fancy-background.js")` works from anywhere. Your script's `OPTIONS` object is protected: an included file's own OPTIONS block does not replace it. |

### app

| Member | Meaning |
| --- | --- |
| `app.activeDocument` | The active document, or `undefined` when none is open. |
| `app.documents` | Every open document. |
| `app.open(path)` | Opens a file and returns its document; throws on failure. Unattended RAW/PDF opens use default settings. |
| `app.newDocument(width, height)` | Creates a new document. |
| `app.alert(text)` | Message box (logs to the console in command-line runs). |
| `app.prompt(text, defaultValue)` | Text input; `null` when cancelled, the default in command-line runs. |
| `app.chooseFolder(title)` | Folder picker; `""` when cancelled or unattended. |
| `app.chooseOpenFile(title, filter)` / `app.chooseSaveFile(title, filter)` | File pickers; the filter uses Qt syntax like `"Images (*.png *.jpg)"`. |
| `app.runCommand(id)` | Triggers a menu command by its stable id, e.g. `app.runCommand("file.scripts.editor")`. `app.commandIds()` lists them all. Returns false for unknown or disabled commands and for `edit.undo`, `edit.redo`, and `file.quit`. |
| `app.undoEnabled` | Set `false` before the first edit to skip the undo snapshot for speed (games, huge batches). Those edits cannot be undone. Resets to `true` each run. |
| `app.version` / `app.apiVersion` | Patchy's version string and the scripting API version (currently 1). |

### Documents

| Member | Meaning |
| --- | --- |
| `doc.width` / `doc.height` / `doc.resolution` | Size in pixels and pixels per inch. |
| `doc.name` / `doc.path` | Title and file path (empty until saved). |
| `doc.layers` | Top-level layers, bottom to top. Groups expose `.children`. |
| `doc.activeLayer` | Get or set the targeted layer. |
| `doc.addLayer(name)` | New empty pixel layer on top, made active. |
| `doc.addTextLayer(text, options)` | Text layer through the real text engine. Options: `font`, `size`, `x`, `y`, `color`, `bold`, `italic`. `size` is the text height in document pixels. |
| `doc.findLayer(name)` | First layer with that exact name, or `undefined`. |
| `doc.combineShapes(layers, op)` | Combine Shapes: merges sibling shape layers into the bottom-most one and returns it. `op` is `"unite"`, `"subtract"` (front shapes cut from the base), `"intersect"`, or `"exclude"`. |
| `doc.selection` | The selection object (below). |
| `doc.flatten()` | Flattens the document. |
| `doc.resizeImage(w, h)` / `doc.resizeCanvas(w, h)` / `doc.crop(x, y, w, h)` | Geometry operations. `crop` clips to the canvas and throws for a disjoint rectangle. |
| `doc.saveAs(path)` / `doc.exportAs(path)` | Saves to the path; the format follows the extension (`.psd`, `.png`, `.jpg`, ...). |
| `doc.close()` | Closes without prompting. |
| `doc.activate()` | Makes this the active tab. |

### Layers

| Member | Meaning |
| --- | --- |
| `layer.name` / `layer.opacity` / `layer.visible` / `layer.locked` | The layer-panel basics. Opacity is 0..100. |
| `layer.blendMode` | Blend mode id string, e.g. `"multiply"` (full list in `patchy.d.ts`). |
| `layer.x` / `layer.y` / `layer.moveTo(x, y)` | Content offset in document pixels. Moving via `x`/`y` is cheap, so animate sprites this way. |
| `layer.bounds` | The content bounding box. |
| `layer.isGroup` / `layer.children` / `layer.isText` / `layer.text` | Group and text access. Setting `text` re-renders the layer. |
| `layer.duplicate()` / `layer.remove()` | Copy above itself, or delete. |
| `layer.ungroup()` | Releases a folder's layers into its parent (top to bottom) and removes the folder. |
| `layer.fill(color)` | Fills the selection (or everything on an empty layer). |
| `layer.fillRect(x, y, w, h, color)` | Overwrites one rectangle (sides up to 30000). RGB8 photos and RGBA8 layers are supported. A transparent color like `"#00000000"` clears. |
| `layer.applyFilter(id, params)` | Runs a filter, e.g. `layer.applyFilter("patchy.filters.gaussian_blur", { radius: 8 })`. |
| `layer.getPixels()` / `layer.setPixels(imageData)` | Raw RGBA8 pixel access. `getPixels` returns `{x, y, width, height, data}` with an `ArrayBuffer` of `width * height * 4` bytes; `setPixels` replaces the layer's pixels with such a block. |
| `layer.traceToShapes(options)` | Trace Image to Shapes: turns the pixel layer into a group of solid shape layers, one per color, and returns the group (the source layer is hidden). Options: `mode` (`"color"`, `"grayscale"`, `"blackAndWhite"`), `colors` (2..256), `threshold`, `paths`, `corners`, `noise`, `smoothing` (denoise blur px), `mergeColors` (merge traced colors within this per-channel difference, 0 = off), `maxAnchors` (anchor budget, 0 = unlimited), `method` (`"abutting"` or `"overlapping"`), `snapCurvesToLines`, `ignoreWhite`, `paletteFromLayer` (with a selection: colors from the whole layer when true, the default; `false` picks colors only from the selected pixels). With a selection active only the selected area is traced. Example: `layer.traceToShapes({ mode: "blackAndWhite", ignoreWhite: true })`. |
| `layer.simplifyPath(options)` | Simplify Path: refits a shape layer's path (or its vector mask) with fewer points. Options: `tolerance` (px, default 1), `cornerAngle` (degrees, default 60), `snapCurvesToLines`. Returns `{anchorsBefore, anchorsAfter}`. |

Colors everywhere are CSS-style strings: `"#rrggbb"`, `"#aarrggbb"`, or named colors like `"red"`.

### Selection

| Member | Meaning |
| --- | --- |
| `doc.selection.exists` / `doc.selection.bounds` | Whether something is selected, and its box. |
| `selectAll()` / `deselect()` | The classics. |
| `selectRect(x, y, w, h)` / `selectEllipse(x, y, w, h)` | Shape selections (sides up to 30000). |

### Dialogs and UI (patchy.ui)

| Member | Meaning |
| --- | --- |
| `patchy.ui.showOptions(spec)` | The standard options dialog described above: defaults, `--script-arg` overrides, and unattended runs handled for you. |
| `patchy.ui.showDialog(spec)` | The same form dialog without the override logic, for mid-script questions. |
| `patchy.ui.createCanvas(options)` | Opens an interactive window with a `graphics` surface plus `onFrame`, key, and mouse callbacks. This is how the bundled games work; see `Games/pong.js` for a compact example. The run stays alive until the window closes. |
| `patchy.ui.playTone(freq, ms, volume, wave)` | Plays a short synthesized blip (defaults 880 Hz, 120 ms, 0.5; wave `"sine"` or `"square"`). Fire-and-forget; great for game feedback - Pong uses it for paddle hits and scores. |
| `patchy.ui.playSound(path)` | Plays a `.wav` file (10 MB max). Relative paths resolve like `include()`. Throws if the file is missing or not a WAV. |
| `patchy.ui.setWindowSize(w, h)` | Resizes the main window. Meant for automation that captures the app at a known size. |
| `patchy.ui.setSidePanelWidth(px)` | Sets the width of the right panel stack (Layers/Channels/Paths). |
| `patchy.ui.captureWindow(path)` | Saves a PNG screenshot of the main window without raising or focusing it. Returns false if the file could not be written. |
| `patchy.ui.setStatusMessage(text)` | Shows a message in the status bar. Handy for progress readouts in long batches. |
| `patchy.ui.zoom` | The active document's view zoom in percent (read/write, 0 with no document). Setting clamps to 5..12800; throws for NaN, non-positive values, or with no document. Only window captures see it. |
| `patchy.ui.fitOnScreen()` | View > Fit on Screen for the active document. Throws with no document. |

Sound is best-effort per platform: Windows and macOS play through the OS directly, Linux needs `paplay`, `pw-play`, or `aplay` on the PATH (most desktops have one). No sound device just means silence, never an error, and setting the environment variable `PATCHY_NO_SOUND=1` mutes scripts entirely.

### Files (patchy.io)

| Member | Meaning |
| --- | --- |
| `patchy.io.readTextFile(path)` / `patchy.io.writeTextFile(path, text)` | Plain text in and out (throws on failure; reads are capped at 256 MB). |
| `patchy.io.listFiles(dir, pattern)` | File names in a folder matching `"*.png"`-style patterns, sorted. |
| `patchy.io.fileExists(path)` / `patchy.io.fileSize(path)` | Whether a file exists, and its size in bytes (-1 when missing). Never throw. |
| `patchy.io.makeDir(path)` / `patchy.io.deleteFile(path)` | Create a folder (with parents) or remove one file; both return true on success. |

### Command-line arguments (patchy.args)

Each `--script-arg key=value` on the command line becomes `patchy.args.key` (always a string). `patchy.isMainScript()` is `true` in the script the user ran and `false` inside an `include()`d file, so one file can be both a library and a runnable script.

## Command line

```text
patchy [--headless] --run-script <file.js> [--script-output out.txt] [--script-arg key=value ...] [files...]
```

| Flag | Meaning |
| --- | --- |
| `--run-script <file.js>` | The script to run. |
| `--script-output <out.txt>` | Console output, errors, and a final `[done]` or `[failed]` line are written here when the run completes. |
| `--script-arg key=value` | Passed to the script as `patchy.args.key` and as a `showOptions` override. Repeatable. |
| `--headless` | Run with no display (the Qt offscreen platform). Never reuses a running Patchy; needs `--run-script`, `--export`, `--stress-test`, or `--screenshot`. |
| `files...` | Opened before the script runs, so the last one is the active document. |

Behavior worth knowing:

- **If Patchy is already running**, the request is forwarded to that instance and the command returns immediately; poll the `--script-output` file for completion. Otherwise a new instance runs the script and exits with code 0 on success or 4 on a script error.
- Command-line runs are **unattended**: dialogs never appear. `showOptions` and `showDialog` return normalized values (choice text, normalized colors, clamped numbers, empty text and false checkboxes), `alert` logs, `prompt` returns its default, and pickers return `""`. Scripts written with the OPTIONS pattern work in both worlds automatically.
- **Headless runs** (`--headless`) use no display and never hand the job to a running Patchy, so they are safe on servers, in CI, and while a Patchy window is open; the exit code and output file always belong to the run itself. On Windows and macOS they see only Patchy's bundled fonts (Windows additionally loads installed families on demand from the font registry), while Linux sees the fontconfig fonts. Sound is muted. `--headless` needs one of `--run-script`, `--export`, `--stress-test`, or `--screenshot` and exits with code 2 otherwise.
- Plain `console.log` lines reach the output file unprefixed, so a script can emit clean machine-readable data (JSON included). Warnings get `[warn] `, errors `[error] `.

Three real examples:

```text
patchy --run-script "Effects/duotone.js" photo.png
patchy --run-script "Utilities/batch-export.js" --script-output result.txt --script-arg folder=C:\photos --script-arg out=C:\photos\web --script-arg format=jpg
patchy --headless --run-script refresh-text.js --script-output result.txt input.psd
```

The easiest way to get a working command: select the script in the Script Manager and click the **C:\\** toolbar button. It shows a copyable command line with the full program path filled in (script authors control the example's arguments with the `@cli` directive).

**AI agents**: this command line is the intended control surface. Write a `.js` file, run it with `--run-script --script-output`, and poll the output file. Point the agent at `patchy.d.ts` and this guide.

## Long-running scripts

There is **no runtime limit**. A batch job may run for hours. The watchdog only stops a script that shows no sign of life (no pixel write, no file operation, no console output) for 2 minutes, which is what a stuck `while (true) {}` looks like. Inside heavy pure-JS computation, call `console.log` with progress now and then; that both feeds the watchdog and updates the busy panel.

When a GUI run stays busy for more than half a second, Patchy shows a progress panel with the script's last console line and a Stop button, so users are never stuck staring at a frozen app. Stopping offers to undo the changes the script made so far.

## Where scripts live

- **Your scripts**: the user scripts folder (File > Scripts > Browse Scripts Folder). Subfolders become submenus.
- **Bundled scripts**: shipped read-only next to the application, in `Games/`, `Demos/`, `Effects/`, and `Utilities/`.
- **Editing a bundled script** never touches the shipped file: Save writes a copy into your user folder at the same relative path, and that copy runs instead, tagged "modified". Right-click it for **Revert to Bundled**.

The bundled scripts double as examples. Good starting points: `Effects/duotone.js` (pixel processing), `Utilities/watermark.js` (text layers and options), `Utilities/batch-export.js` (folder batch work), `Games/pong.js` (interactive windows).

Input and unattended-run details:

- Form keys must be nonempty strings. `selectRect` clips to the canvas; an outside rectangle clears the selection. Layer positions must fit signed 32-bit coordinates, including their bounds. Setting a text layer's `text` to `""` clears the ink.
- Forwarded CLI scripts use the same unattended rules as fresh headless runs. Menu dialogs cancel without appearing. Closing a modified document through `file.close` is refused; `doc.close()` explicitly closes without prompting.
- Script canvas mouse callbacks use button values 1 (left), 2 (right), and 4 (middle). Move callbacks report a bit mask of held buttons.
- The accepted blend modes include `dissolve`. Script color strings keep Qt's eight-digit `#AARRGGBB` order.
