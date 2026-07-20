# Patchy Scripting Guide

Patchy has a built-in JavaScript engine that can automate almost anything you can do by hand: edit layers and pixels, run filters, add text, save and export, batch-process whole folders, and even open small game windows. Scripts are plain `.js` files (modern ES6-level JavaScript), and this guide covers everything you need to write them.

For exact type signatures, see `patchy.d.ts` in the same folder as this guide. It is the machine-readable version of this document, and editors like VS Code use it for autocomplete.

## Running scripts

There are three ways to run a script:

1. **The File > Scripts menu.** Every bundled script and every script in your user scripts folder shows up here, organized by folder.
2. **The Script Manager** (File > Scripts > Script Manager). A folder tree, a code editor with syntax highlighting, a console, and Run/Stop buttons. Press **F5** to run what is in the editor. This is the best place to write and test scripts.
3. **The command line.** `patchy --run-script myscript.js` runs a script unattended, for batch jobs and external tools. See the Command line section below. The Script Manager's **C:\\** toolbar button shows a ready-made command line for any script.

A script run is **one undo entry**: no matter how many edits a script makes, one Ctrl+Z puts the document back the way it was.

## Your first script

Open the Script Manager, paste this into the editor, and press F5 with a document open:

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
| `include(path)` | Runs another script file in the same scope. Relative paths resolve against the running script, then your user scripts folder, then the bundled scripts, so `include("Effects/fancy-background.js")` works from anywhere. |

### app

| Member | Meaning |
| --- | --- |
| `app.activeDocument` | The active document, or `undefined` when none is open. |
| `app.documents` | Every open document. |
| `app.open(path)` | Opens a file and returns its document. |
| `app.newDocument(width, height)` | Creates a new document. |
| `app.alert(text)` | Message box (logs to the console in command-line runs). |
| `app.prompt(text, defaultValue)` | Text input; `null` when cancelled, the default in command-line runs. |
| `app.chooseFolder(title)` | Folder picker; `""` when cancelled or unattended. |
| `app.chooseOpenFile(title, filter)` / `app.chooseSaveFile(title, filter)` | File pickers; the filter uses Qt syntax like `"Images (*.png *.jpg)"`. |
| `app.runCommand(id)` | Triggers a menu command by its stable id, e.g. `app.runCommand("file.scripts.editor")`. `app.commandIds()` lists them all. |
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
| `doc.selection` | The selection object (below). |
| `doc.flatten()` | Flattens the document. |
| `doc.resizeImage(w, h)` / `doc.resizeCanvas(w, h)` / `doc.crop(x, y, w, h)` | Geometry operations. |
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
| `layer.fill(color)` | Fills the selection (or everything on an empty layer). |
| `layer.fillRect(x, y, w, h, color)` | Overwrites one rectangle. A transparent color like `"#00000000"` clears. |
| `layer.applyFilter(id, params)` | Runs a filter, e.g. `layer.applyFilter("patchy.filters.gaussian_blur", { radius: 8 })`. |
| `layer.getPixels()` / `layer.setPixels(imageData)` | Raw RGBA8 pixel access. `getPixels` returns `{x, y, width, height, data}` with an `ArrayBuffer` of `width * height * 4` bytes; `setPixels` replaces the layer's pixels with such a block. |

Colors everywhere are CSS-style strings: `"#rrggbb"`, `"#aarrggbb"`, or named colors like `"red"`.

### Selection

| Member | Meaning |
| --- | --- |
| `doc.selection.exists` / `doc.selection.bounds` | Whether something is selected, and its box. |
| `selectAll()` / `deselect()` | The classics. |
| `selectRect(x, y, w, h)` / `selectEllipse(x, y, w, h)` | Shape selections. |

### Dialogs and UI (patchy.ui)

| Member | Meaning |
| --- | --- |
| `patchy.ui.showOptions(spec)` | The standard options dialog described above: defaults, `--script-arg` overrides, and unattended runs handled for you. |
| `patchy.ui.showDialog(spec)` | The same form dialog without the override logic, for mid-script questions. |
| `patchy.ui.createCanvas(options)` | Opens an interactive window with a `graphics` surface plus `onFrame`, key, and mouse callbacks. This is how the bundled games work; see `Games/pong.js` for a compact example. The run stays alive until the window closes. |

### Files (patchy.io)

| Member | Meaning |
| --- | --- |
| `patchy.io.readTextFile(path)` / `patchy.io.writeTextFile(path, text)` | Plain text in and out (throws on failure). |
| `patchy.io.listFiles(dir, pattern)` | File names in a folder matching `"*.png"`-style patterns, sorted. |

### Command-line arguments (patchy.args)

Each `--script-arg key=value` on the command line becomes `patchy.args.key` (always a string). `patchy.isMainScript()` is `true` in the script the user ran and `false` inside an `include()`d file, so one file can be both a library and a runnable script.

## Command line

```text
patchy --run-script <file.js> [--script-output out.txt] [--script-arg key=value ...] [files...]
```

| Flag | Meaning |
| --- | --- |
| `--run-script <file.js>` | The script to run. |
| `--script-output <out.txt>` | Console output, errors, and a final `[done]` or `[failed]` line are written here when the run completes. |
| `--script-arg key=value` | Passed to the script as `patchy.args.key` and as a `showOptions` override. Repeatable. |
| `files...` | Opened before the script runs, so the last one is the active document. |

Behavior worth knowing:

- **If Patchy is already running**, the request is forwarded to that instance and the command returns immediately; poll the `--script-output` file for completion. Otherwise a new instance runs the script and exits with code 0 on success or 4 on a script error.
- Command-line runs are **unattended**: dialogs never appear. `showOptions` returns its effective values, `alert` logs, `prompt` returns its default, and pickers return `""`. Scripts written with the OPTIONS pattern work in both worlds automatically.
- Plain `console.log` lines reach the output file unprefixed, so a script can emit clean machine-readable data (JSON included). Warnings get `[warn] `, errors `[error] `.

Two real examples:

```text
patchy --run-script "Effects/duotone.js" photo.png
patchy --run-script "Utilities/batch-export.js" --script-output result.txt --script-arg folder=C:\photos --script-arg out=C:\photos\web --script-arg format=jpg
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
