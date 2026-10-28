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

**Pause/Resume** beside Stop holds visible MCP or command-line automation after the current native edit. Pausing appears until that edit finishes. Once Resume appears, you can paint, move layers, change settings, use Undo, or close documents. Finish an active manual gesture before resuming. Manual edits have their own Undo steps; later automation starts a new group. Resume uses the changed workspace, and reports missing or incompatible targets. Scripts should inspect again when they depend on cached geometry. Window movement, zoom, pan, layer scrolling/filtering, menu browsing, Preferences and About stay available while working. Conflicting edits explain that Pause is required; applying Preferences requires closing the dialog and pausing first. Stop remains available. `patchy.ui.paused` shares the button state and resets at run end. Paused time does not advance simulated brush time. Hidden work and ordinary interactive scripts reject enabling Pause. A paused MCP request stays busy, so resume using the window button.

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

With no arguments, each `patchy-mcp` connection owns an offscreen workspace. Documents and undo history stay open across requests; JavaScript globals reset for every script. Save files explicitly before disconnecting. The connector isolates settings and disables single-instance forwarding, sound, and update checks.

With `--attach`, the connector uses your existing Patchy window. Closing that
window leaves MCP discovery and help available. A `workspace_unavailable` error
means a workspace could not be attached: open the matching Patchy and call
`get_info` again. Connected responses include `workspaceAvailable: true`. Read
tools can reconnect; edits require a fresh `expectedState` and never initiate
attachment. An interrupted request reports `workspace_disconnected` with
`retrySafe: false`. Inspect the document before repeating an edit because it may
have made changes. Requests are never replayed into a new workspace.

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

`drawStrokes` uses native Brush, Eraser, or Mixer strokes, including bitmap tips, complete Brush dynamics, the Brush `mode` (any blend mode id except `pass-through`, plus `behind` and `clear`), Wet Edges, pressure/tilt/rotation, spacing, smoothing, Flow accumulation, selections, alpha locks, and palette snapping. It temporarily sets its own brush settings and restores the artist's settings. It requires an unlocked 8-bit RGB/RGBA pixel layer; text, vectors, Smart Objects, and groups must first be converted or painted on a separate pixel layer. Point coordinates are document pixels. Missing pressure means ordinary full-pressure mouse behavior; supplied pressure uses the default pen size/opacity mapping (20% size floor, 15% opacity floor). Size-one paths use exact pixel segments. Timed airbrush requires complete nondecreasing `timeMs` samples starting at zero. Smoothing and airbrush use simulated time, independent of `present()` delays. Use `patchy.brushes.listTips()`, `listPresets()`, and `resolve()` before painting; `renderPreview()` saves native swatches without document edits. `getCurrent()` captures the artist's brush explicitly, while `activate()` deliberately changes it. `createTip`, `importAbr`, and saved-preset operations persist resources outside document Undo. Saved presets snapshot their tip and appear in the UI. Mixer uses its native full opacity and Flow; ordinary Brush dynamics do not apply to Mixer/Eraser. Read `PatchyStroke` in `patchy.d.ts` for defaults, fields, and limits. Unknown fields and malformed batches fail before painting. For exact sprites, use palette-colored RGBA arrays with `setPixels` or rectangles with `fillRect`.

Inspect with `get_preview`: it returns PNG image content plus the crop rectangle and X/Y scale. The JavaScript equivalent writes a PNG without changing the document's save path or modified state:

```js
var preview = doc.renderPreview(patchy.args.preview, {
  rect: {x: 0, y: 0, width: 32, height: 32},
  maxWidth: 256, maxHeight: 256, nearestNeighbor: true
});
patchy.setResult(preview);
```

Preview dimensions default to a 1024 by 1024 bounding box and may be 1 through 4096. The aspect ratio is preserved. Ordinary previews shrink as needed; nearest-neighbor previews may enlarge pixel art. Crop rectangles are clipped to the canvas. For full-resolution outputs beyond the preview bound, use `exportAs`, which writes a copy and leaves the document's path and modified state alone. MCP `get_preview` with `target: "window"` is an offscreen app-window render for inspecting the interface.

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

To paint on pixels, explicitly target an unlocked pixel layer (8-bit for `fillPath`;
strokes paint 16 and 32-bit layers at their depth) and call
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
| `@hotkey` | The script's default keyboard shortcut in Qt's portable spelling (`Ctrl+Alt+D`, `Shift+F6`). See "Hotkeys for scripts" below. |

A 128x128 PNG next to the script with the same base name (`myscript.js` and `myscript.png`) becomes its icon. Right-click a script in the Script Manager for **Set Icon from Current Window**.

## Hotkeys for scripts

Any script in the Scripts menu can have a keyboard shortcut, so a one-keypress job such as "export this document as a .dds next to its file" (`Utilities/quick-export-dds.js`) needs no menu trip. Two ways to set one:

- **In the script**: a `// @hotkey Ctrl+Alt+D` line in the header comment is the script's default shortcut. A key Patchy already uses stays with Patchy (the first registered command wins a tie), and Preferences shows a note on the script's row saying which command took it.
- **In Preferences > Hotkeys**: every script is listed under **Scripts** by its display name. Click the shortcut chip and press the key. Right-click a script in the Script Manager and choose **Assign Hotkey...** to open that page on the script's row. A shortcut chosen here always wins over the `@hotkey` line, and "Use here instead" takes a key from another command.

The binding is stored in your Patchy settings under a stable id built from the script's path inside the scripts folder (`script.` plus the percent-encoded relative path, such as `script.Utilities%2Fquick-export-dds.js`), so it survives updating Patchy, editing the script, and saving a bundled script as your own copy (the copy keeps the bundled script's path). Renaming or moving the file starts a new id: assign the key again. A shortcut for a script that no longer exists is dropped the next time you press OK in Preferences.

Bundled scripts ship without shortcuts (they could collide with yours). The menu entry shows a script's shortcut once one is bound, and the hotkey works from the moment Patchy starts, without opening the menu first. A running script cannot start another one: `app.runCommand` refuses the `script.` ids (use `include()` to reuse code).

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
| `patchy` | The namespace: `patchy.ui`, `patchy.io`, `patchy.args`, `patchy.scripts`, `patchy.plugins`, `patchy.gmic`, `patchy.recovery`, `patchy.brushes`, `patchy.setResult()`, `patchy.isMainScript()`, `patchy.version`, `patchy.apiVersion`. |
| `console` | `log`, `info`, `warn`, `error`; output goes to the Script Manager console and to `--script-output`. |
| `setTimeout` / `setInterval` / `clearTimeout` / `clearInterval` / `requestAnimationFrame` | Timers, like in a browser. The run stays alive while timers are pending. |
| `include(path)` | Runs another script file in the same scope. Relative paths resolve against the running script, then your user scripts folder, then the bundled scripts, so `include("Effects/fancy-background.js")` works from anywhere. Your script's `OPTIONS` object is protected: an included file's own OPTIONS block does not replace it. |

### app

| Member | Meaning |
| --- | --- |
| `app.activeDocument` | The active document, or `undefined` when none is open. |
| `app.documents` | Every open document. |
| `app.open(path)` | Opens a file and returns its document; throws on failure. RAW opens read the photo's `.rawprefs` sidecar or use current defaults, without writing settings. Unattended PDF opens use default settings. |
| `app.newDocument(width, height, options?)` | Creates a new document with a white Background. Options: `name` (the tab title and default Save As name; characters a file name cannot hold are dropped) and `colorMode` (`"rgb"`, the default, or `"cmykProof"`, Image > Mode > CMYK Proof, which needs a CMYK profile in Preferences). |
| `app.alert(text)` | Message box (logs to the console in command-line runs). |
| `app.prompt(text, defaultValue)` | Text input; `null` when cancelled, the default in command-line runs. |
| `app.chooseFolder(title)` | Folder picker; `""` when cancelled or unattended. |
| `app.chooseOpenFile(title, filter)` / `app.chooseSaveFile(title, filter)` | File pickers; the filter uses Qt syntax like `"Images (*.png *.jpg)"`. |
| `app.listFonts()` | Every font family available to the text engine, as `{family, styles, writingSystems}` sorted by family. Check it before `addTextLayer` when the exact face matters: `app.listFonts().filter(function (f) { return f.writingSystems.indexOf("Japanese") >= 0; })`. |
| `app.displayInfo()` | One entry per screen, primary first: `{name, x, y, width, height, devicePixelRatio, primary, hdrQueried, hdrEnabled, headroom, potentialHeadroom, sdrWhiteNits, peakNits, fullFrameNits, minNits, remoteSession}`. `headroom` is how many times brighter than SDR white the screen can go (1 on SDR screens). Windows reports nits; macOS reports only the headroom ratios. `hdrQueried` is false on Linux, the web build and offscreen runs. |
| `app.colorToCmyk(color)`, `app.cmykToColor(c, m, y, k)` | Converts one color to ink percentages (`{c, m, y, k}`, 0 to 100) and back to `"#rrggbb"`, through the working CMYK profile chosen in Preferences. `app.cmykProfile` describes it: `{name, hasProfile, intent, blackPointCompensation}`. Documents stay RGB; with `hasProfile` false the numbers come from an approximate formula. |
| `app.runCommand(id)` | Triggers a menu command by its stable id, e.g. `app.runCommand("file.scripts.editor")`. `app.commandIds()` lists them all. Returns false for unknown or disabled commands and for `edit.undo`, `edit.redo`, and `file.quit`. |
| `app.downloadWatermarkModels()` | Downloads the Adobe TrustMark watermark models (135 MB from Adobe, kept in Patchy's data folder) if they are missing, waiting with progress in the status bar. `app.watermarkModelsAvailable` tells whether they are present. |
| `app.saveMetadataPreferences` / `app.setSaveMetadataPreferences({mode, name, copyright, email, website, askBeforeAdding})` | Preferences > Metadata and Watermarks, "Saved metadata": `mode` `"none"`, `"basic"` (the PSD save stamp) or `"personal"` (also the name and copyright, added to files that do not name you yet; scripted saves never ask, so with `askBeforeAdding` they add nothing personal). |
| `app.watermarkPreferences` / `app.setWatermarkPreferences({text, addOnSave, checkOnOpen})` | Preferences > Watermarks: the TrustMark text saves add, `addOnSave` `"never"`, `"ask"` or `"always"` (scripted saves never ask), and the background check of opened images. |
| `app.downloadObjectSelectionModel()` | Downloads Object Selection's High Quality model (234 MB from GitHub, kept in Patchy's data folder) if it is missing, waiting with progress in the status bar. Returns true once it is present; throws if the download fails, and in the web build. |
| `app.exportPdf(documents, path, options)` | Writes one PDF with a page per document (an array, or one document), each page sized from that document's pixels and resolution. Options: `lossless` (default true; false writes JPEG), `imageQuality` (`"lossless"`, `"high"`, `"medium"`, or `"low"`; wins over `lossless`), `keepOriginalImageData` (default true: an unchanged page imported from a PDF keeps its original image bytes), `editableLayers` (keep shapes and text as PDF objects, default false), `missingFontsAsImages`. Throws on bad arguments or a write failure. A folder of files becomes a PDF by opening each with `app.open` and passing the documents here. |
| `app.undoEnabled` | Set `false` before the first edit to skip the undo snapshot for speed (games, huge batches). Those edits cannot be undone. Resets to `true` each run. |
| `app.version` / `app.apiVersion` | Patchy's version string and the scripting API version (currently 1). |

### Documents

| Member | Meaning |
| --- | --- |
| `doc.width` / `doc.height` / `doc.resolution` | Size in pixels and pixels per inch. |
| `doc.bitDepth` / `doc.convertBitDepth(bits)` | Bits per channel (8, 16 or 32; 32 is floating point in linear light) and Image > Mode's conversion. 16 and 32-bit files open at their depth; new documents start at 8 bits. Indexed documents stay 8-bit, and the web version stops at 16. Pixel access (`getPixels`/`setPixels`) stays 8-bit RGBA at every depth. Converting to 32 bits sets layers in blend modes Photoshop refuses at 32 bits (such as Screen and Overlay) to Normal, and setting one of those modes on a 32-bit document throws. |
| `doc.colorProfile` / `doc.convertToProfile(profile, options)` | The name of the document's RGB profile (`"sRGB IEC61966-2.1"` when it has none) and Edit > Convert to Color Profile. `profile` is `"sRGB"` or the path of an RGB `.icc`/`.icm` file; options `intent` (`"perceptual"`, `"relative-colorimetric"` (default), `"saturation"`, `"absolute-colorimetric"`) and `blackPointCompensation` (default true). Pixels convert and the profile becomes the embedded one; text, shape, fill and adjustment colors and layer styles keep their values. Files in another RGB profile already open converted to sRGB unless Preferences turns that off. |
| `doc.name` / `doc.path` | Title and file path (empty until saved). |
| `doc.importNotes` | The notes the file's reader left when it opened: what it dropped or approximated, as the status bar shows them. Empty for new documents. |
| `doc.layers` | Top-level layers, bottom to top. Groups expose `.children`. |
| `doc.activeLayer` | Get or set the targeted layer. |
| `doc.addLayer(name)` | New empty pixel layer on top, made active. |
| `doc.addTextLayer(text, options)` | Text layer through the real text engine. `text` is a string or an array of runs `{text, font?, size?, bold?, italic?, color?}`, each typed in its own format on top of the options, so one layer mixes faces, sizes and colors: `doc.addTextLayer([{text: "Hold the "}, {text: "LEFT TRIGGER", bold: true}], {font: "Bahnschrift", size: 40, x: 100, y: 200})`. Options: `font`, `size`, `x`, `y`, `color`, `bold`, `italic`, `orientation` (`"horizontal"` or `"vertical"`), `direction` (`"auto"`, `"ltr"`, `"rtl"`), `box` (`{width, height}`, at least 16 px each: a paragraph text box with `x`/`y` as its top-left corner, wrapping lines at the width like a Type-tool drag), `align` (`"left"`, `"center"`, `"right"`, `"justify"`). `size` is the text height in document pixels; for vertical text `x`/`y` is the first column's top centre. `font` is a family name, or family plus face such as `"Arial Black"` (on Windows a face's full or PostScript name works too); compact family spellings such as `LiberationSans` resolve to `Liberation Sans` without a substitution warning; a font that is not installed, or that has no glyph for any character of the text (the bundled Noto Naskh Arabic has no Latin letters), renders in a fallback and logs a warning. Any text may contain `\n`: every line lands in the same layer as a new paragraph, so a heading and its subline need no second layer. Photoshop's paragraph list styles (bullets, numbering) are not modeled: type a bullet character such as `"\u2022 "` at the start of the line; an empty spacer line keeps the run's own size (a bigger spacer run only makes that gap taller). Prefer real text layers over drawing letters as shapes: they stay editable in Patchy and Photoshop. |
| `layer.text`, `layer.textRuns`, `layer.setTextRuns(runs)`, `layer.textBox`, `layer.textAlign` | Editing existing text layers. Every setter here logs a console warning, like `addTextLayer`, when a font is not installed or has no glyphs for the text; setting `text` on a layer whose font is missing replaces that font with the substitute it was drawn in. `text` reads the plain text and setting it retypes the layer with the first character's formatting. `textRuns` reads the formatted runs (`{text, font, style, size, bold, italic, color, caps, script, underline, strikethrough, ordinals, fractions}`) and `setTextRuns(runs)` replaces the content with runs, each starting from the first character's formatting plus its own `font`, `size`, `bold`, `italic`, `color` and Character panel styles: `caps` (`"all"`, `"small"`, `"none"`), `script` (`"superscript"`, `"subscript"`, `"none"`), `underline`, `strikethrough`, `ordinals`, `fractions` (a bare string is a run with no overrides): `layer.setTextRuns([{text: "Ask "}, {text: "Seth", bold: true}, {text: " for a game"}])`. `textBox` is `{width, height}` for paragraph text, `null` for point text; `textAlign` reads or sets the paragraph alignment. Every edit re-renders through the same session the Type tool uses, one undo step. |
| `layer.textParagraph`, `addTextLayer(text, {paragraph})` | Photoshop's Paragraph panel metrics in document pixels: `{firstLineIndent, startIndent, endIndent, spaceBefore, spaceAfter}`. Reading gives the first paragraph's values; setting merges the fields you pass into every paragraph and re-renders (`layer.textParagraph = {startIndent: 24, firstLineIndent: -24}` is a hanging indent; `spaceAfter: 12` opens the paragraphs up). The `paragraph` option of `addTextLayer` takes the same object. |
| `doc.importFilesAsLayers(paths)` | Files as Layers: each path (a string or an array of strings) becomes a layer directly above the active layer, bottom to top in argument order, the last file ending on top and active. A multi-layer file (a PSD, an animated GIF) becomes a folder named after it. Pixels keep their size: a file the document's size lands exactly, others center on the canvas. Throws and adds nothing when a file cannot be read. Returns the new layers in argument order. |
| `doc.addSmartObject(path, options)` | Place Embedded / Place Linked: the file (PSD, PSB, PNG, JPEG, TIFF, BMP, SVG, ...) becomes a smart-object layer on top, made active. The default embeds a copy of the file; `linked: true` references the file on disk instead, so editing the file and calling `layer.updateSmartObject()` refreshes the layer, and placing the same file linked again shares that reference (one update refreshes every layer placed from it). With no position or size the file lands at its physical size, centered, scaled down to fit a smaller canvas, like the menu commands. `x`/`y` place the top-left corner (an omitted axis centers); `width`/`height` set the placed size in document pixels, one alone keeping the aspect ratio; `scale` multiplies the physical size (1 = 100%) and is ignored when a size is given; `name` overrides the layer name. SVG contents render sharp at any size. Throws, adding nothing, for a file that cannot be read, a size outside 1..30000 pixels, or an unknown option. A linked path is stored relative to the document's folder when the document is saved as PSD or PSB, so a folder holding the PSD and its files moves as a unit: `doc.addSmartObject("logo.svg", {linked: true, x: 40, y: 40, width: 300})`. |
| `layer.isSmartObject`, `layer.getSmartObject()`, `layer.updateSmartObject()` | Smart-object state and Update Smart Object Content. `getSmartObject()` returns `{linked, fileName, path, relativePath, missing, changed, sourceId, width, height, resolution, quad}` (`null` for other layers): `path` is the linked file as it resolves now (the stored absolute path when `missing`), `changed` says the file on disk differs from what the layer shows, `sourceId` is shared by every layer placed from one linked file, and `quad` holds the placement corners in document pixels. `updateSmartObject()` re-reads the linked file and re-renders every layer sharing its source, returning how many; it throws for embedded contents or a missing file. Relink and Embed Linked remain menu commands (`app.runCommand`). |
| `doc.addTextLayer(text, {area: path})` | Flow text inside one closed `PatchyVectorPath` in document coordinates. The boundary supplies its position; `area` and `box` cannot be combined. The source path is copied. Concave and self-intersecting contours can have several spans on a line. |
| `layer.textArea` | Detached boundary snapshot in document coordinates, or `null`. Assign one closed contour to reflow the existing text through its normal undoable text session. Assign `null` to convert area text to box text. Overflow text stays editable. |
| `doc.findLayer(name)` | First layer with that exact name, or `undefined`. |
| `doc.combineShapes(layers, op)` | Combine Shapes: merges sibling shape layers into the bottom-most one and returns it. `op` is `"unite"`, `"subtract"` (front shapes cut from the base), `"intersect"`, or `"exclude"`. |
| `doc.mergeLayers(layers, options?)` | Merges the supplied layers and selected groups' contents without a dialog. Options `keepVectors`, `withinGroups`, `separateVectorTypes` default to `true`, `false`, `true`. Vector parts retain their colors, strokes, opacity and paint alignment. Enable `withinGroups` to retain folders. `separateVectorTypes` separates solid, gradient, pattern and mixed-paint categories; turn it off to combine these appearances in one vector layer. Returns surviving selected leaf layers in bottom-to-top order. A single leaf is unchanged. Set `keepVectors:false` to rasterize merges. Masks, effects, clipping, locks and stacking order can require additional layers. `singleVector:true` overrides the three options: at least two vectors become one at the bottommost source's stack position, even across unselected layers. Individual effects are removed unless `effectsFrom` names a vector included in the merge, whose stack applies once to the combined silhouette. Incompatible masks, clipping, blending, locks and group boundaries throw before mutation. |
| `doc.alignLayers(edge, options?)` | Layer > Arrange > Align. `edge` is `"left"`, `"hcenter"`, `"right"`, `"top"`, `"vcenter"`, or `"bottom"`. Options: `layers` (this document's layers; default the layer selection) and `alignTo` (`"selection"`, the default, or `"canvas"`). With a selection the layers align to it; a single layer, or `alignTo: "canvas"`, aligns to the canvas; otherwise the layers align among themselves. A group moves as one unit. Returns the number of layers moved. |
| `doc.distributeLayers(mode, options?)` | Layer > Arrange > Distribute over three or more layers or groups. `mode` adds `"hspacing"` and `"vspacing"` to the Align edges: feature modes keep the outermost units and space the rest evenly; spacing modes share one equal gap. Option `layers` as above. Throws with fewer than three movable units. |
| `doc.selection` | The selection object (below). |
| `doc.flatten()` | Flattens the document. |
| `doc.resizeImage(w, h, {method?})` / `doc.resizeCanvas(w, h)` / `doc.crop(x, y, w, h)` | Geometry operations. `resizeImage` takes a resampling `method` id (`"automatic"`, `"nearest"`, `"bilinear"`, `"bicubic"`, `"bicubicSmoother"`, `"bicubicSharper"`; default automatic). `crop` clips to the canvas and throws for a disjoint rectangle. |
| `doc.trim({basedOn?, tolerance?, top?, bottom?, left?, right?})` | Image > Trim. Cuts away the edges where every pixel is transparent (`basedOn: "transparent"`, the default) or exactly the color of the `"topLeft"` or `"bottomRight"` pixel, on the chosen sides (all by default). `tolerance` (0 to 255, default 0) also cuts near matches: alpha up to the value, or every channel within it of the corner color. Returns the kept `{x, y, width, height}`, or `null` when nothing was trimmed. |
| `doc.saveAs(path, options?)` | Saves to the path; the format follows the extension (`.psd`, `.png`, `.jpg`, ...). The document then points at that file; a layered document saved to a flat format keeps its own path (save-a-copy). `.ora` (OpenRaster) keeps the layer tree, baking what it cannot hold (text, effects, masks, adjustment layers) into layer pixels without asking. `options` sets the format's save options with no dialog (below); without it the save uses your Save Options defaults. WebP stays a single flattened image. |
| `doc.exportAs(path, options?)` | Writes a copy with the same formats and options as `saveAs` and leaves the document alone: path, title and modified state never change. |
| `doc.encodeForWeb(format, options?)` | Encodes the image as File > Export for Web would, without a file: `format` is `"jpeg"`, `"webp"`, `"png"`, `"gif"`, `"avif"` or `"jxl"`, `options` are that format's `exportAs` keys. Returns `{bytes, width, height}` with `bytes` an ArrayBuffer, for example to compare sizes. |
| `doc.exportAnimatedWebp(path, options?)` | Exports visible top-level layers top first, with each group rendered as one frame. Leaves the document path and modified state unchanged. |
| `doc.exportAnimatedPng(path, options?)` | The same frames as an animated PNG (APNG). Options `frameDelayMs` and `loopCount`, as for WebP. |
| `doc.close()` | Closes without prompting. |
| `doc.activate()` | Makes this the active tab. |

### Save options

`saveAs` and `exportAs` take an options object holding the same choices the format's Save Options dialog offers, so a script can write exactly the file it wants with no dialog:

```js
doc.exportAs("hero.dds", {compression: "bc3", mipmaps: "on"});
doc.exportAs("web.jpg", {quality: 85});
doc.exportAs("web.webp", {quality: 80, lossless: false});
doc.exportAs("app.ico", {sizes: [16, 32, 48, 256], resample: "smooth"});
doc.exportAs("page.pdf", {imageQuality: "high", editableLayers: true});
```

| Extension | Keys |
| --- | --- |
| `.jpg`, `.jpeg` | `quality` (0 to 100), `progressive` and `optimizeCoding` (smaller web files, both off by default), `thumbnail` (the EXIF preview thumbnail, on by default; written when EXIF is kept, and with every `metadata` choice when set to `true` explicitly) |
| `.webp` | `quality` (0 to 100), `lossless` |
| `.jxr` | `quality` (1 to 100), `lossless` |
| `.jxl` | `quality` (1 to 100), `lossless`, `effort` (1 to 9), `animate` (visible top-level layers become frames, top first), `frameDelayMs`, `loopCount` (0 loops forever) |
| `.avif` | `quality` (0 to 100), `lossless`, `speed` (0 to 10, lower is smaller and slower) |
| `.dds` | `compression` (`"auto"`, `"uncompressed"`, `"bc1"`, `"bc3"`, `"bc4"`, `"bc5"`, `"bc7"`), `mipmaps` (`"auto"`, `"on"`, `"off"`). Automatic keeps an opened .dds file's format and otherwise picks BC1 for opaque images and BC3 with transparency. |
| `.ico`, `.cur` | `sizes` (an array from 16, 24, 32, 48, 64, 128, 256), `resample` (`"auto"`, `"nearest"`, `"smooth"`); `.cur` also `hotspot: {x, y}` |
| `.bmp` | `encoding` (`"rgba32"`, `"rgb24"`, `"indexed8"`, `"indexed4"`, `"indexed2"`), `paletteMode` (`"exact"`, `"quantize"`, `"paletteFile"`), `palettePath` |
| `.rttex` | `encoding` (`"rgba8"`, `"rgba4444"`, `"jpeg"`), `quality` (1 to 100), `powerOfTwo` (`"pad"`, `"stretch"`, `"none"`), `forceSquare`, `forceAlpha`, `compress` |
| `.pdf` | the `app.exportPdf` keys: `imageQuality`, `lossless`, `editableLayers`, `keepOriginalImageData`, `missingFontsAsImages`, `pages` (`"artboards"`, the default: one page per artboard when there are any; `"groups"`: one page per top-level group, hidden ones included, so a PDF imported as page folders goes back page by page; `"canvas"`: one page), `artboardPages` (the older true/false spelling) |
| `.gif` | `animate` (visible top-level layers become frames, top first), `frameDelayMs`, `colors` (2 to 256), `dither` (`"none"`, `"diffusion"`), `ditherAmount` (0 to 100) |
| `.png` | `pngColors` (`"full"`, the default, or `"palette"` for an indexed PNG up to 256 colors), and with `"palette"` also `colors`, `dither`, `ditherAmount`; `animate` (an animated PNG from the visible top-level layers, top first, full color), `frameDelayMs`, `loopCount` (0 loops forever) |
| `.tif`, `.tiff` | `layers`: keep the layers the way Photoshop does (the flattened image plus Photoshop's layer data; Photoshop and Patchy open the layers, other apps show the flattened image). `saveAs` defaults to `true` when the document has layers a flat file would lose; `exportAs` defaults to `false`. A TIFF with layers is RGB, so `layers: true` with `colorMode: "cmyk"` throws. |

`.jpg`, `.png`, `.tif`, `.webp`, `.gif`, `.jxl` and `.avif` also take `metadata` (`"all"`, the default; `"copyright"`; `"basic"`, only the program, dates and the file's format, size and resolution; `"none"`): the File Info metadata written with the image. The color profile and resolution are always written.

`.jpg`, `.tif` and flattened `.pdf` also take `colorMode` (`"rgb"`, the default, or `"cmyk"`): CMYK for print, separated through the working CMYK profile from Preferences, which the file then embeds. Transparent areas become paper white. Add `blackInkOnly: true` to print black text and grays with black ink alone instead of four-ink rich black. Without `colorMode`, a document in CMYK Proof mode (`doc.cmykProof = true`, Image > Mode > CMYK Proof) saves those formats in CMYK and every other document saves RGB. Patchy opens a CMYK file as RGB again.

Keys you leave out follow your Save Options defaults and the document (a document opened from a .dds keeps that file's format under `"auto"`). The object is strict: a key that does not apply to the extension, an unknown key, a wrong type or an out-of-range value throws and nothing is written, so a typo never saves with defaults. A scripted save never changes your Save Options defaults. Formats without options (`.psd`, `.tga`, ...) accept no keys.

### File Info (metadata)

`doc.fileInfo` holds the File > File Info fields (title, authors, description, keywords, copyright, IPTC location and contact fields). `doc.setFileInfo({...})` changes any of them; `doc.metadataEntries()` lists every EXIF, XMP, IPTC, ICC and text item with a privacy class; `doc.xmp` is the XMP packet. `doc.stripMetadata()` removes everything but the color profile (`{keepColorProfile: false}` removes that too), and `doc.removeLocation()` / `doc.removeEditHistory()` remove parts. Changes reach the file on the next save. `doc.scanForWatermarks()` checks the pixels for the open Stable Diffusion invisible watermarks and returns `{scanned, matches: [{id, name, bits, differingBits, redBlueSwapped}], trustmark: {available, marks: [{variant, schema, text, id, bits, correctedBits, confidence}]}, checks}`; TrustMark is read once the watermark models are downloaded (`app.downloadWatermarkModels()`). `checks` has one row per kind checked, as File Info shows them: `{id, name, status, reason, detail}` with `status` `"found"`, `"notFound"` or `"notChecked"` (then `reason` says why, such as `"modelsNotDownloaded"` or `"imageTooSmall"`). Watermarks declared in Content Credentials are the `metadataEntries()` rows whose privacy is `"watermark"`. `doc.addInvisibleWatermark({text: "Ann 2026"})` hides a short message in the active layer with TrustMark (`{id: "0x..."}` or no payload for a random ID; `schema`, `strength`); anyone with a TrustMark reader can read it.

```js
const doc = app.activeDocument;
doc.setFileInfo({title: "Harbor at dawn", authors: ["Ann Example"], keywords: ["harbor", "dawn"],
                 copyrightStatus: "copyrighted", copyrightNotice: "(c) 2026 Ann Example"});
if (doc.metadataEntries().some(e => e.privacy === "location")) doc.removeLocation();
doc.exportAs("web.jpg", {quality: 85, metadata: "copyright"});
```

### Animated WebP

Animated WebP example (also available through MCP `execute_script`):

```js
app.activeDocument.exportAnimatedWebp("animation.webp", {
  frameDelayMs: 100, loopCount: 0, quality: 75, lossless: false
});
```

Those are the defaults, except `loopCount` uses the document's imported or last exported
count when available. Zero means forever; a positive count includes the first play.
Layer names ending in seconds, such as `blink 0.033s`, override the default delay.
Delays are integer milliseconds (0 through 16777215), play counts are integers
0 through 65535, and quality is an integer 0 through 100. Quality 100 also selects
lossless. Invalid options, an output extension other than `.webp`, or an export failure
throw an error. Hidden layers are skipped. Identical consecutive frames can be combined
by the encoder. Loop counts are retained for the open document, not in PSD or recovery files.

`doc.exportAnimatedPng(path, options?)` writes the same frames as an animated PNG
(APNG: lossless 8-bit RGBA, frame 1 doubles as the still image for viewers without
animation support). It takes `frameDelayMs` and `loopCount` with the same ranges and
defaults; the path must end in `.png` or `.apng`. Every layer stays one frame.

```js
app.activeDocument.exportAnimatedPng("animation.png", { frameDelayMs: 100, loopCount: 0 });
```

### Palettes and indexed PNG

`doc.setPalette(colors, options?)` sets 1 to 256 opaque color strings in export
order, including duplicates. Options are `enabled` (default `true`) and
`alphaThreshold` (integer 0 to 255, default 128). Unknown options throw.
Optional `names` is an array parallel to `colors`; use empty strings for unnamed
swatches. Labels are single lines of at most 4096 UTF-8 bytes. `setPalette`
without `names` clears labels; `loadPalette` preserves file labels by default.
This undoable operation preserves existing layer pixels and editable content.
Enabled mode snaps tool writes, the display, and PNG export to the palette;
filters and live effects can still produce off-palette layer pixels.
`enabled:false` attaches the colors without constraining editing or export.

`doc.getPalette()` returns a detached snapshot with `colors`, `names`, `enabled`,
`alphaThreshold` (`null` when disabled), and `sourceBitDepth`, or `null` when
there is no palette. Imported indexed PNG/BMP/GIF palettes are readable even
when editing mode is off. Source depth describes the attached table, not the
document's internal RGBA pixel storage.

`doc.loadPalette(path, options?)` uses the native palette reader and applies
the same options as `setPalette`, returning the resulting palette. It reads
PAL, GPL, HEX, ACT, ACO, ASE, and indexed BMP. GPL color names are imported;
format-specific transparency indexes are not. `doc.savePalette(path, name?)` writes
PAL, GPL, HEX, ACT, or ACO, returning true or throwing on failure. Saving a
palette does not change document history, path, or modified status.
Use GPL to preserve color names in a palette file. Names also persist in Patchy's
optional indexed PNG text metadata. Other image editors may discard that
metadata. Palette controls and eyedropper readouts show exact-match
names alongside color codes. Right-click editable swatches for Set Name/Rename;
an empty name clears the label. Document renames are undoable.

**Embedding a named palette in a PSD:** first attach it with `setPalette` or
`loadPalette`, then call `saveAs` with a `.psd` path. Saving automatically embeds
the colors, names, and palette-mode settings, even when `enabled` is false.
Patchy restores them on reopen without needing a companion GPL file. The PSD
retains normal RGB layers with optional Patchy metadata; it does not become a
Photoshop indexed-mode document or install Photoshop named swatches. A color
array used only to draw pixels is not an attached document palette.

All PSD/PSB output must open in Adobe Photoshop without warnings or errors.
Custom metadata is allowed only when it causes no warning, repair, or
data-discard prompt. Use Patchy's native saver. A successful Patchy round trip
does not establish warning-free Photoshop opening; report actual verification
and do not treat suppressed Photoshop dialogs as proof. Other editors may
discard Patchy metadata when resaving.

```js
var doc = app.activeDocument;
if (!doc) throw new Error("No active document");
doc.loadPalette("beads_palette.gpl");
// Alternatively: doc.setPalette(["#FFFFFF", "#000000"], {
//     names: ["しろ WHITE", "くろ BLACK"]
// });
if (!doc.saveAs("skeleton_indexed.png")) throw new Error("Indexed PNG save failed");
if (!doc.saveAs("skeleton.psd")) throw new Error("PSD save failed");
patchy.setResult({path: doc.path, palette: doc.getPalette()});
```

Verify a saved copy by reopening it and comparing `getPalette()` colors, names,
`enabled`, and `alphaThreshold` with the pre-save snapshot. Keep unsaved user
documents open. Names preserve Unicode, including mixed Japanese and English;
do not substitute translations for labels the user asked to retain exactly.

Use `saveAs`/`exportAs` for indexed PNG. `renderPreview` writes a truecolor
preview. PNG export may reserve one extra palette entry for transparency.

### Layers

| Member | Meaning |
| --- | --- |
| `layer.name` / `layer.opacity` / `layer.visible` / `layer.locked` | The layer-panel basics. Opacity is 0..100. |
| `layer.blendMode` | Blend mode id string, e.g. `"multiply"` (full list in `patchy.d.ts`). |
| `layer.colorLabel` | The Layers panel color label: `"none"`, `"red"`, `"orange"`, `"yellow"`, `"green"`, `"blue"`, `"violet"` or `"gray"`. PSD saves keep it as Photoshop's layer color. |
| `layer.x` / `layer.y` / `layer.moveTo(x, y)` | Content offset in document pixels. Moving via `x`/`y` is cheap, so animate sprites this way. Fractions round like Photoshop (halves up); layers always sit on whole pixels. A move works like the Move tool: a text layer's anchor, a shape's path, a smart object's placement and a linked mask travel with the pixels (an unlinked mask stays put), so later edits and saved PSDs keep the new position. Moving a group moves every layer inside it; a group has no position of its own, so `group.moveTo(dx, dy)` offsets its contents. |
| `layer.transform({matrix} or {quad}, interpolation?)` | Edit > Free Transform in one call, with the tool's rules for every layer type and one undo step with the rest of the script. `matrix` (`[a, b, c, d, e, f]`, x' = a x + c y + e, y' = b x + d y + f) is applied to the layer's transform box; `quad` gives the box's new corners in document pixels (top left, top right, bottom right, bottom left), so any four points skew, distort or apply perspective: `layer.transform({ quad: [[100, 70], [260, 70], [300, 210], [100, 180]] })`. Smart objects keep a perspective placement; type layers take only a parallelogram. With a selection on a pixel layer only the selected pixels transform (the box is the selection bounds) and the selection follows them. `interpolation` takes the `resizeImage` method ids. |
| `layer.bounds` | The content bounding box. |
| `layer.isGroup` / `layer.children` / `layer.isText` / `layer.text` | Group and text access. Setting `text` re-renders the layer; the new text keeps the first character's formatting. |
| `layer.textOrientation` / `layer.textDirection` | Text layers: `"horizontal"` or `"vertical"`, and the paragraph direction `"auto"`, `"ltr"` or `"rtl"`. Setting either re-renders the layer. |
| `layer.rerenderSmartObject()` | Embedded smart objects: renders the layer again from the file it stores, for every layer sharing that source, and returns how many were re-rendered. A smart object opened from a PSD shows the pixels saved in the file until then. Linked ones use `updateSmartObject()`. |
| `layer.rerenderText()` | Text layers: renders the layer again from its stored text and formatting without changing them. A type layer opened from a PSD shows the pixels saved in the file until it is edited; this replaces them with Patchy's own render. |
| `layer.resolvedTextFonts()` | Text layers: one `{text, font, family, style}` per run, where `font` is the font the run names and `family` and `style` are the face Patchy really draws it in on this machine. Use it to see a substituted or missing font, for example `layer.resolvedTextFonts()[0].style` is `"Black"` for a PSD run in Arial Black. |
| `layer.textFont` | Text layers: the font family name the layer uses (read-only; `""` for other layers). |
| `layer.duplicate(targetDocument?)` / `layer.remove()` | Copy above itself, or into another open document above its active layer; or delete. |
| `layer.ungroup()` | Releases a folder's layers into its parent (top to bottom) and removes the folder. |
| `layer.fill(color)` | Fills the selection (or everything on an empty layer); a feathered or soft-edged selection blends the fill in by its coverage. |
| `layer.fillRect(x, y, w, h, color)` | Overwrites one rectangle (sides up to 30000). RGB8 photos and RGBA8 layers are supported. A transparent color like `"#00000000"` clears. |
| `layer.applyCameraRawFilter(settings)` | Filter > Camera Raw Filter, e.g. `layer.applyCameraRawFilter({ exposure: 0.5, contrast: 20, vibrance: 30 })`. Settings: `temperature`, `tint` (relative, -100 to 100), `exposure` (-5 to 5 EV), `contrast`, `highlights`, `shadows`, `saturation`, `vibrance` (-100 to 100). Respects the selection like `applyFilter`. |
| `layer.applyFilter(id, params)` | Runs a filter, e.g. `layer.applyFilter("patchy.filters.gaussian_blur", { radius: 8 })`. With a selection only the selected pixels change, soft edges blended by coverage, as with the Filter menu. |
| `layer.removeBackground()` | Layer > Layer Mask > Remove Background: Select Subject on the merged document, set as this layer's mask (an existing mask is multiplied). Pixel, adjustment, or group layers; the selection is unchanged. |
| `layer.redEye({x, y, width?, height?, pupilSize?, darkenAmount?})` | The Red Eye tool: `{x, y}` clicks there (document pixels), adding `width` and `height` drags a box instead. The connected red area at or nearest the point is desaturated and darkened by a fixed color rule (no eye detection); a selection limits the change. `pupilSize` and `darkenAmount` are 1..100, default 50. The layer must be the active layer. Returns `{pupilPixels, x, y, width, height}` (the changed bounds); throws when no red pixel is found. |
| `layer.removeObject(options?)` | Edit > Remove Object without its dialog: fills the document selection from its surroundings. `method` is `"contentAware"` (default: the deterministic exemplar fill, an exhaustive best-patch search nearby, no AI; falls back to nearest edge when nothing clean is in reach) or `"nearestEdge"` (the selection form of Spot Healing; calling it again on the same selection tries the next source, `{ attempt: n }` picks one, 0-based, wrapping). For content-aware, `attempt` is the variation (0 = best match, each n > 0 a different reproducible fill, the dialog's Reroll), `toneMatch` (0..100, default 0) scales the tone match after the fill (0 = raw fill), and `feather` (px, default 0) softens the fill's edge outward. The layer must be the active layer and a selection must exist. Returns `{method, patches, source, sourceCount, attempt}`. |
| `layer.getPixels()` / `layer.setPixels(imageData)` | Raw RGBA8 pixel access. `getPixels` returns `{x, y, width, height, data}` with an `ArrayBuffer` of `width * height * 4` bytes; `setPixels` replaces the layer's pixels with such a block. |
| `layer.traceToShapes(options)` | Trace Image to Shapes: turns the pixel layer into a group of solid shape layers, one per color, and returns the group (the source layer is hidden). Options: `mode` (`"color"`, `"grayscale"`, `"blackAndWhite"`), `colors` (2..256), `threshold`, `paths`, `corners`, `noise`, `smoothing` (denoise blur px), `mergeColors` (merge traced colors within this per-channel difference, 0 = off), `maxAnchors` (anchor budget, 0 = unlimited), `method` (`"abutting"` or `"overlapping"`), `snapCurvesToLines`, `ignoreWhite`, `paletteFromLayer` (with a selection: colors from the whole layer when true, the default; `false` picks colors only from the selected pixels). With a selection active only the selected area is traced. Example: `layer.traceToShapes({ mode: "blackAndWhite", ignoreWhite: true })`. |
| `layer.simplifyPath(options)` | Simplify Path: refits a shape layer's path (or its vector mask) with fewer points. Options: `tolerance` (px, default 1), `cornerAngle` (degrees, default 60), `snapCurvesToLines`. Returns `{anchorsBefore, anchorsAfter}`. |

Colors everywhere are CSS-style strings: `"#rrggbb"`, `"#aarrggbb"`, or named colors like `"red"`.

### Artboards

An artboard is a top-level group with its own canvas rect and background, like Photoshop's: its layers show only inside the rect, over its background. Layers outside every artboard still render. The canvas grows to hold every artboard; when it grows up or left, every document coordinate moves by the same amount, so read positions again after such an edit.

| Member | Meaning |
| --- | --- |
| `doc.artboards` | The artboards, top of the layer stack first. |
| `doc.addArtboard(name, {x, y, width, height}, {background?, color?})` | Layer > New > Artboard. `background` is `"white"` (the default), `"black"`, `"transparent"` or `"color"`; `color` alone means `"color"`. An empty name gives "Artboard N". Returns the new artboard. |
| `doc.artboardFromLayers(layers, name?)` | Moves the layers onto a new artboard sized to them. Throws for an artboard or a layer already on one. |
| `layer.isArtboard` / `layer.artboard` | Whether the layer is an artboard, and its `{x, y, width, height, background, color}` (`null` otherwise). |
| `layer.setArtboard(options)` | Changes an artboard's rect or background (keys you leave out keep their value), makes a top-level group an artboard, or with `null` turns an artboard back into a group. `layer.moveTo`, `x` and `y` move an artboard with everything on it; `bounds` is its rect. |
| `doc.fitCanvasToArtboards()` | Image > Fit Canvas to Artboards: the canvas becomes the artboards plus any visible layer outside them. Returns `{x, y}`, how far every coordinate moved. |
| `doc.exportArtboards(folder, {format?, prefix?, artboards?, overwrite?})` | File > Export > Artboards to Files: one file per artboard, named `prefix` (default the document name and " - ") plus the artboard's name. `format` is `"png"` by default; `"psd"` keeps layers, `"pdf"` writes a page each. A name already in the folder gets " 2", " 3" unless `overwrite` is true. Returns the written paths. |

```js
var doc = app.newDocument(100, 100);
var phone = doc.addArtboard("Phone", {x: 0, y: 0, width: 390, height: 844});
var tablet = doc.addArtboard("Tablet", {x: 490, y: 0, width: 820, height: 1180}, {color: "#f2efe8"});
doc.exportAs("screens.pdf");  // two pages, one per artboard
doc.exportArtboards("out", {format: "png"});
```

### Selection

| Member | Meaning |
| --- | --- |
| `doc.selection.exists` / `doc.selection.bounds` | Whether something is selected, and its box. |
| `selectAll()` / `deselect()` | The classics. On a layer inside an artboard, All is that artboard. |
| `selectRect(x, y, w, h)` / `selectEllipse(x, y, w, h)` | Shape selections (sides up to 30000). |
| `selectSubject(options?)` | Select > Subject: the main subject from the bundled model, one pass over the merged document (several seconds on the CPU). Options: `operation` (`"replace"`, `"add"`, `"subtract"`, `"intersect"`), `feather`, `antialias`. |
| `selectBackground(options?)` | Select > Select Background: everything `selectSubject` would not select. Same options. `selectSubject`, `selectBackground` and `layer.removeBackground` also take `sampleAllLayers` (default true; false analyzes the active layer alone). |
| `objectSelect(options)` | The Object Selection tool: `{rect: {x, y, width, height}}` selects the object in the box, `{point: {x, y}}` the object under the point (exactly one of the two). Also `operation` (`"replace"`, `"add"`, `"subtract"`), `sampleAllLayers` (default true), `feather`, `antialias`. A rect that is small against the image is analyzed with its surroundings at full resolution, like the tool. |
| `automaticSelectionAvailable` | False where the build has no automatic selection; the calls above then throw. The web build has it: its first `objectSelect` downloads the models (about 19 MB, cached by the browser afterward). |
| `subjectSelectionAvailable` | True when the build includes Select Subject's model. Current builds leave it out, so `selectSubject`, `selectBackground` and `layer.removeBackground` throw while `objectSelect` still works. |
| `objectSelectionModel` | `"standard"` (the built-in model) or `"highQuality"`: the model `objectSelect` runs. Desktop builds use High Quality once it is downloaded (`app.downloadObjectSelectionModel()`, or from the tool), unless the Object Selection tool's Model setting is Standard. |

### Dialogs and UI (patchy.ui)

| Member | Meaning |
| --- | --- |
| `patchy.ui.showOptions(spec)` | The standard options dialog described above: defaults, `--script-arg` overrides, and unattended runs handled for you. |
| `patchy.ui.showDialog(spec)` | The same form dialog without the override logic, for mid-script questions. |
| `patchy.ui.createCanvas(options)` | Opens an interactive window with a `graphics` surface plus `onFrame`, key, and mouse callbacks. This is how the bundled games work; see `Games/pong.js` for a compact example. The run stays alive until the window closes. |
| `patchy.ui.playTone(freq, ms, volume, wave)` | Plays a short synthesized blip (defaults 880 Hz, 120 ms, 0.5; wave `"sine"` or `"square"`). Fire-and-forget, for game feedback: Pong uses it for paddle hits and scores. |
| `patchy.ui.playSound(path)` | Plays a `.wav` file (10 MB max). Relative paths resolve like `include()`. Throws if the file is missing or not a WAV. |
| `patchy.ui.setWindowSize(w, h)` | Resizes the main window. Meant for automation that captures the app at a known size. |
| `patchy.ui.setSidePanelWidth(px)` | Sets the width of the right panel stack (Layers/Channels/Paths). |
| `patchy.ui.captureWindow(path)` | Saves a PNG screenshot without raising or focusing the window. Waits up to 60 seconds for enabled Dynamic Vector Preview to settle; returns false on timeout or write failure. |
| `patchy.ui.setStatusMessage(text)` | Shows a message in the status bar. Handy for progress readouts in long batches. |
| `patchy.ui.expandPanel(name)` | Opens a panel of the right column like its Window menu entry: reopens it if closed, brings its tab to the front, and expands it. Names: `"layers"`, `"channels"`, `"paths"`, `"history"`, `"properties"`, `"info"`, `"palette"`, `"color"` (exact and lowercase; anything else throws). Layers, Channels and Paths share one tab group, so they expand together. Use it before `captureWindow` to show, say, the Properties panel. |
| `patchy.ui.collapsePanel(name)` / `patchy.ui.closePanel(name)` | Collapse the panel to its title row, or close it. Same names. |
| `patchy.ui.panelState(name)` | `{open, front, expanded}` for the panel: whether it is open (a tab behind another counts), its group's front tab, and expanded. |
| `patchy.ui.proofColors`, `patchy.ui.gamutWarning` | View > Proof Colors and View > Gamut Warning (read/write booleans). They change how every canvas is displayed, through the working CMYK profile: a soft proof of the print, and flat gray over colors the profile cannot reproduce. Pixels, previews, saves and exports are unchanged, so only window captures see them. `app.colorInCmykGamut(color)` answers the gamut question for one color. |
| `patchy.ui.pixelGrid` | View > Show > Pixel Grid (read/write boolean, saved, on by default): thin lines between pixels at 600% zoom and up, each tinted from its own pixel so it shows on light and dark pixels. A view only; `app.runCommand("view.pixel_grid")` toggles it too. |
| `patchy.ui.toolPalette` | Window > Customize Tool Palette (read/write, saved): `{hiddenTools, layout, wheelScroll, ungroupTools, showGearButton, hiddenLayerButtons}` (`showGearButton` no longer does anything: it reads false and setting it is ignored). Assign any subset, e.g. `patchy.ui.toolPalette = {hiddenTools: ["tools.hand"], layout: "twoColumns"}`. Tool ids are hotkey ids (`"tools.hand"`); hidden tools keep their shortcuts. `layout` is `"oneColumn"` or `"twoColumns"`; `wheelScroll` scrolls a palette too short for its tools; `ungroupTools` (false by default) gives every tool its own button, with no tool group flyouts; layer button ids are `"new_layer"`, `"new_folder"`, `"new_adjustment"`, `"layer_styles"`, `"add_mask"`, `"duplicate"`, `"animation"`, `"delete"`. Unknown ids or layouts throw. |
| `patchy.ui.zoom` | The active document's view zoom in percent (read/write, 0 with no document). Setting clamps to 0.01..12800; throws for NaN, non-positive values, or with no document. Only window captures see it. |
| `patchy.ui.fitOnScreen()` | View > Fit on Screen for the active document. Throws with no document. |
| `patchy.ui.previewExposure` | The active 32-bit document's preview exposure in stops (read/write), the status bar's Exposure box. It changes what the canvas, thumbnails and 8-bit exports show, never the pixels; not saved, not an undo step. Reads 0 without a 32-bit document; setting clamps to -20..20 and throws for NaN or without one. |
| `app.runCommand("view.vector_preview")` | Toggles View > Dynamic Vector Preview, also available in Preferences > Application. Vector shapes and masks stay sharp alongside pixel content; saved/exported pixels keep document resolution. This is a persisted application view option. Connector sessions refuse `runCommand`. |

Sound is best-effort per platform: Windows and macOS play through the OS directly, Linux needs `paplay`, `pw-play`, or `aplay` on the PATH (most desktops have one). No sound device just means silence, never an error, and setting the environment variable `PATCHY_NO_SOUND=1` mutes scripts entirely.

### Files (patchy.io)

| Member | Meaning |
| --- | --- |
| `patchy.io.readTextFile(path)` / `patchy.io.writeTextFile(path, text)` | Plain text in and out (throws on failure; reads are capped at 256 MB). A write replaces the file whole through a temporary and a rename, so a failed write keeps the previous file. |
| `patchy.io.listFiles(dir, pattern)` | File names in a folder matching `"*.png"`-style patterns, sorted. |
| `patchy.io.fileExists(path)` / `patchy.io.fileSize(path)` | Whether a file exists, and its size in bytes (-1 when missing). Never throw. |
| `patchy.io.makeDir(path)` / `patchy.io.deleteFile(path)` | Create a folder (with parents) or remove one file; both return true on success. |

### Recovery (patchy.recovery)

Patchy writes a PSB copy of every modified document to a recovery folder on a timer (Preferences > Application, on by default every 10 minutes) and reopens the copies after a crash. These members let a script force a write, inspect the store, and drive the reopen. The web build has no store: `enabled` is false and every list is empty.

| Member | Meaning |
| --- | --- |
| `patchy.recovery.enabled` / `patchy.recovery.intervalMinutes` | The Preferences values (persisted). The interval must be 5, 10, 15, 30, or 60. |
| `patchy.recovery.directory` | This instance's recovery folder. |
| `patchy.recovery.writeNow()` | Writes a copy of every modified document whose state changed since its last copy and waits for the files. Returns the PSB paths written (empty when nothing changed or a dialog or gesture was active). |
| `patchy.recovery.listFiles()` | `{file, title, originalPath, savedAt}` for each copy this instance holds. |
| `patchy.recovery.listOrphaned()` | The same, plus `directory`, for copies left by instances that no longer run. |
| `patchy.recovery.recoverAll()` | Reopens every orphaned copy as a modified "(Recovered)" document and returns the documents. |
| `patchy.recovery.discardOrphaned()` | Deletes every orphaned folder; returns how many documents were dropped. |

### Legacy Photoshop plug-ins (patchy.plugins)

Windows builds run classic Photoshop filter plug-ins (`.8bf`, 32-bit and 64-bit) found in the `plugins` folder next to `patchy.exe`, in `%APPDATA%\RTsoft\Patchy\plugins`, and in the folders added under Preferences > Plug-ins. Scanning never runs a plug-in; running one executes it with your permissions in a separate helper process.

| Member | Meaning |
| --- | --- |
| `patchy.plugins.folder` | The plug-ins folder next to the application (created with its README when read); `""` on macOS and Linux. |
| `patchy.plugins.folders` | The added folders (persisted). Setting it rescans. |
| `patchy.plugins.list()` | `{id, name, category, path, supported, reason, architecture}` for every plug-in file the last scan saw. |
| `patchy.plugins.rescan()` | Rescans every folder and returns `list()`. |
| `layer.applyPlugin(id, {dialog, captureDialog})` | Runs the plug-in on an 8 or 16-bit pixel layer inside the selection, one undo step (a 16-bit layer goes through an 8-bit copy; 32-bit layers throw). `{dialog: false}` skips its settings dialog (last or default settings; a plug-in that opens one anyway gets its OK pressed); unattended runs never show it. `{captureDialog: "shot.png"}` saves an image of the plug-in's dialog while it is up (an unattended run shows the dialog for the capture and answers it itself). |

### G'MIC filters (patchy.gmic)

Builds that include the G'MIC engine offer its filter library to scripts. Check
`patchy.gmic.available` first: without the engine `list()` is empty and `applyGmic` throws.

| Call | What it does |
|---|---|
| `patchy.gmic.available` | Whether this build has the G'MIC engine. |
| `patchy.gmic.version` | The G'MIC version the filters come from, such as `"4.0.5"`. |
| `patchy.gmic.list()` | `{id, name, folder, inputLayers, parameters}` for every offered filter. Each parameter is `{name, type, default, minimum, maximum, choices}`. |
| `layer.applyGmic(id, values, {inputLayers})` | Runs the filter on the layer inside the selection and returns the number of layers it added. |

`values` is an array in `list()` order or an object keyed by parameter name; anything left out
keeps its default. Parameter names are G'MIC's own English labels.

```js
var layer = app.activeDocument.activeLayer;
if (patchy.gmic.available) {
  layer.applyGmic("fx_sepia", { "Brightness (%)": 10, "Contrast (%)": 5 });
  // Splits the layer into three: it becomes red, and two layers are added above it.
  var added = layer.applyGmic("fx_channels2layers");
}
```

`inputLayers` (`"active"`, `"activeAndBelow"`, `"activeAndAbove"`, `"all"`, `"visible"`,
`"hidden"`) chooses the layers the filter works on, and every chosen layer it may change is
changed in one undo step. A filter made for one image (Sepia, a blur) runs on each chosen layer
by itself, at that layer's size, exactly as if you applied it to each in turn; any further
results become new layers below the layer they came from (above it when that layer is named
Background, which stays at the bottom). A filter made for several layers
(the Blend filters) gets them lined up on the document, and when it combines them the result
replaces the layer you called `applyGmic` on. Locked, text, shape and Smart Object layers among
the chosen ones are read but never changed.
`applyGmic` throws on a text, shape or Smart Object layer, on a layer with locked pixels, and when
no layer matches `inputLayers` or none of them can be changed.
### Perspective planes (patchy.perspective)

Filter > Perspective Planes from a script: paste a layer onto a flat surface in perspective, or
clone and paint on it with a brush that shrinks with distance. A plane is four `[x, y]` corners
in document pixels, in order around the surface, or the index of one of the document's planes
(the ones the workspace shows). Each edit is clipped to its one plane, as in the workspace, and
writes a new layer named Perspective above `layer` (or onto `layer` with `target: "layer"`),
returning the layer written, or `null` when nothing changed. `layer` must be an 8-bit or
16-bit RGB pixel layer of the active document.

| Call | What it does |
|---|---|
| `patchy.perspective.checkPlane(corners)` | `""` when the corners make a plane, else `"tooSmall"`, `"crossing"` or `"concave"`. |
| `patchy.perspective.getPlanes(doc)` / `setPlanes(doc, planes)` | The document's planes, `[{corners, grid}]` (`grid` 0 is Auto). Kept while the document is open, never saved in the file. |
| `patchy.perspective.paste(layer, {plane, source, placement, target})` | Pastes the `source` layer's pixels onto the plane. `placement` is `"fill"` (default: stretched to the plane's corners), `"fit"` (centred, upright, inside the plane) or `{x, y, scale, rotation}` (center in plane units, which are pixels along the plane's nearest side; degrees). |
| `patchy.perspective.placeLayer(layer, {plane, placement, target})` | Place Layer: takes `layer`'s own content off it and puts it on the plane (`placement` as for `paste`, default `"fill"`). It replaces the layer unless `target` is `"newLayer"`, which moves the content to a new layer above. Needs an 8-bit layer with transparency. |
| `patchy.perspective.clone(layer, {plane, sourcePlane, from, points, size, hardness, opacity, sample, target})` | Clone stroke along `points` (document pixels) on `plane`; `from` is the source point (where you would Alt-click), read through `sourcePlane` (default: `plane`). The first point lines up with `from`. `sample` is the Clone tool's Sample choice: `"current"` (default, the layer alone), `"currentAndBelow"` or `"all"` (everything visible). |
| `patchy.perspective.paint(layer, {plane, points, color, size, hardness, opacity, target})` | Paint stroke with `color` (`"#rrggbb"` or `[r, g, b]`, default black). |

`size` is the brush diameter in plane units (default 60), `hardness` 0 to 100 (50) and `opacity`
1 to 100 (100). Corners that cannot be a plane, and a locked layer, throw.

```js
var doc = app.activeDocument;
var wall = [[120, 60], [560, 140], [560, 400], [120, 470]];
var poster = doc.layers[1];  // the picture to paste
patchy.perspective.paste(doc.activeLayer, {plane: wall, source: poster, placement: "fit"});
patchy.perspective.setPlanes(doc, [wall]);
```

### The script library (patchy.scripts)

Scripts can manage scripts: list what the Scripts menu shows, write a new one into your scripts folder, and put it on a key. This is how an agent driving Patchy through MCP answers "make a script that does X and assign it to Ctrl+Alt+X":

```js
patchy.scripts.install("Mine/flatten-and-export.js",
  "// @name Flatten and Export\n" +
  "var d = app.activeDocument; if (d) d.exportAs(d.path.replace(/\\.[^.]+$/, '') + '.png');\n",
  {hotkey: "Ctrl+Alt+X"});
```

| Member | Meaning |
| --- | --- |
| `patchy.scripts.userFolder`, `bundledFolder` | The two script folders ("/" separators). |
| `patchy.scripts.list()` / `rescan()` | Every script, bundled first: `{name, fileName, relativePath, path, bundled, modified, description, author, hotkey, defaultHotkey, commandId}`. Both rescan the folders and refresh the menu and hotkey commands. |
| `patchy.scripts.install(relativePath, source, {hotkey})` | Writes the file below `userFolder` (folders created as needed), rescans, binds the shortcut when given, and returns the entry. Throws for a path outside the folder, a non-.js name, a write failure, or a shortcut Patchy cannot bind. |
| `patchy.scripts.setHotkey(relativePath, shortcut)` | The Preferences > Hotkeys override for the script (`""` clears it, restoring a `// @hotkey` line); returns the shortcut in effect, `""` when another override already owns the key. |
| `patchy.scripts.getHotkey(relativePath)` | The shortcut in effect. |

A `// @hotkey` line in the source is the script's default; `install`'s `hotkey` option is the override that always wins, which is the right choice for an agent, since the user sees and can change it under Preferences > Hotkeys > Scripts. Right-click any entry in File > Scripts for the same actions the Script Manager offers (Run, Edit in Script Manager, Show in Folder, Command Line Example, Assign Hotkey, Set Icon, Revert to Bundled).

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

There is **no runtime limit**. A batch job may run for hours. The watchdog only stops a script that shows no sign of life (no call into Patchy at all: no pixel write, no file operation, no console output) for 2 minutes, which is what a stuck `while (true) {}` looks like. Inside heavy pure-JS computation, call `console.log` with progress now and then; that both feeds the watchdog and updates the busy panel.

When a GUI run stays busy for more than half a second, Patchy shows a progress panel with the script's last console line and a Stop button, so users are never stuck staring at a frozen app. Stopping offers to undo the changes the script made so far.

## Where scripts live

- **Your scripts**: the user scripts folder (File > Scripts > Browse Scripts Folder). Subfolders become submenus.
- **Bundled scripts**: shipped read-only next to the application, in `Games/`, `Demos/`, `Effects/`, and `Utilities/`.
- **Editing a bundled script** never touches the shipped file: Save writes a copy into your user folder at the same relative path, and that copy runs instead, tagged "modified". Right-click it for **Revert to Bundled**.

The bundled scripts double as examples. Good starting points: `Effects/duotone.js` (pixel processing), `Utilities/watermark.js` (text layers and options), `Utilities/batch-export.js` (folder batch work), `Utilities/quick-export-dds.js` (save options and a script on a hotkey), `Games/pong.js` (interactive windows).

Input and unattended-run details:

- Form keys must be nonempty strings. `selectRect` clips to the canvas; an outside rectangle clears the selection. Layer positions must fit signed 32-bit coordinates, including their bounds. Setting a text layer's `text` to `""` clears the ink.
- Forwarded CLI scripts use the same unattended rules as fresh headless runs. Menu dialogs cancel without appearing. Closing a modified document through `file.close` is refused; `doc.close()` explicitly closes without prompting.
- Script canvas mouse callbacks use button values 1 (left), 2 (right), and 4 (middle). Move callbacks report a bit mask of held buttons.
- The accepted blend modes include `dissolve`. Script color strings keep Qt's eight-digit `#AARRGGBB` order.

Merged vector layers expose independent paints in `layer.getShape().parts`. Each part lists its path `groups`, `fill`, `stroke`, `opacity`, `fillOpacity`, `pathDisabled`, `pathInverted` and `wholeCanvas`. These are read-only snapshots; geometry remains in the shared `path`. Whole-layer appearance updates change the supplied fields in all parts while retaining unrelated settings. PSD saves keep the object as native vector children in a marked group; Patchy restores one merged vector layer when it reopens the file.
