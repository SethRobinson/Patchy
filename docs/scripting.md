# JavaScript scripting

Patchy embeds a JavaScript engine (Qt's QJSEngine, from the Qt6Qml module) with its own
document automation API, a Script Editor dialog, bundled example scripts, and a CLI entry
point that lets external tools and AI agents drive a running Patchy. This doc is the
authoritative record of the design rules; the user-facing API reference is
`scripts/patchy.d.ts` (TypeScript definitions, shipped next to the bundled scripts).

## Where things live

- `src/ui/script_engine.{hpp,cpp}`: `ScriptEngineHost`, the engine owner. Run lifecycle,
  bootstrap prelude (console/timers/include/the `patchy` namespace), watchdog, undo and
  refresh integration, and every MainWindow-facing service the wrappers call
  (including the interactive helpers: alert/prompt/pickers/`showDialog`/`runCommand`).
  `ScriptEngineHost` is a friend of MainWindow; the wrappers never touch MainWindow
  directly.
- `src/ui/script_api.{hpp,cpp}`: the QObject wrappers JS sees (`app`, documents, layers,
  selection, `patchy.io`, `patchy.ui`).
- `src/ui/script_canvas_window.{hpp,cpp}`: interactive script windows (games/demos).
- `src/ui/script_editor_dialog.{hpp,cpp}` + `src/ui/js_syntax_highlighter.{hpp,cpp}`: the
  editor UI (folder tree, shadow-override saves, context menu).
- `src/ui/script_folders.{hpp,cpp}`: the script browser model - recursive bundled/user
  folder scans and the shadow-override merge, shared by the File > Scripts menu and the
  editor tree.
- `src/ui/main_window_scripting.cpp`: the File > Scripts menu (subfolders become
  submenus), the editor entry, and the CLI flows (`run_script_command`, `run_cli_script`).
- `scripts/bundled/` (repo): bundled scripts + `patchy.d.ts`, staged next to the binaries
  by the `patchy_bundled_scripts` copy-once target (macOS: `Contents/Resources/scripts`,
  Linux install: `share/patchy/scripts`). ONLY `scripts/bundled` ships - the rest of
  `scripts/` is dev tooling and must never be staged (the copy step cleans the staged
  folder first, so renames/removals propagate to existing build trees). Bundled scripts
  are organized into `Games/`, `Demos/`, `Effects/`, `Utilities/` (display names go
  through `script_folder_display_name` for localization). User scripts live in the
  per-user app-data folder under `scripts/` (`MainWindow::user_scripts_directory()`).
- Tests: `tests/ui/scripting_tests.cpp`.

## Shadow overrides (saving over a bundled script)

Save on a bundled script never touches the shipped file (read-only installs; app updates
would clobber edits): the editor writes a user copy at the SAME relative path under the
user scripts folder (`Games/breakout.js` -> `<user scripts>/Games/breakout.js`). The scan
merge (`scan_scripts`) then shows that copy in place of the bundled entry - tagged
"(modified)" in the menu and tree - and it is what runs from the menu, the editor, and
`include()`. "My Scripts" lists only non-overriding user scripts. Revert to Bundled
(tree context menu) deletes the copy. Keep this override-by-relative-path rule intact
everywhere a bundled script is resolved.

## Engine rules (binding design decisions)

- **One run at a time, on the UI thread.** A run stays alive until the synchronous
  evaluation AND every timer and script canvas window are done. A fresh QJSEngine is
  created per run and destroyed at run end, so nothing leaks between runs and stored
  QJSValues die with their engine (canvas windows drop theirs in teardown first).
- **One undo entry per run and session.** The first mutation a run makes to a session
  pushes one "Script: name" snapshot (`prepare_mutation`); everything after rides it, so
  a 60fps animation undoes to its pre-script state in one step. Scripts can opt out for
  speed with `app.undoEnabled = false` (per-run state, resets to true each run): the
  snapshot is skipped and those edits cannot be undone, but sessions are still marked
  modified so closing protects the work (`breakout.js` uses this).
- **Wrappers hold ids, never pointers.** Layer wrappers keep session id + LayerId and
  re-resolve on every access, throwing a JS error when the target is gone. The layers
  vector reallocates and sessions close; a stored `Layer*` is the historical
  use-after-free pattern. Reads resolve through const documents (mutable layer accessors
  bump revisions on access).
- **Refresh is coalesced.** Mutations mark per-session dirt; one deferred flush per
  event-loop turn repaints the canvas (region or full) and refreshes panels for the
  active session only. Structure changes (add/remove/reorder/rename) rebuild the layer
  panel; pixel-only changes refresh thumbnails.
- **The watchdog is the only defense against `while(true)`.** A helper thread arms
  around every evaluate and callback and calls `QJSEngine::setInterrupted` after the
  timeout (default 30 s; `PATCHY_SCRIPT_TIMEOUT_MS` overrides it, which the tests use).
  Never remove the arm/disarm pairing around a new entry point into script code; route
  new callback invocations through `call_script_callback`.
- **Reentrancy: never destroy the engine from inside script code.** Timer slots and
  window event filters run callbacks; a failing callback schedules a deferred finish
  (`schedule_completion_check` / the deferred `finish_run` path) instead of tearing down
  from inside itself. Stop during evaluation only interrupts; the evaluate caller
  finishes the run.
- **Palette mode**: `setPixels` and `fill` are tool-like writes and snap to the document
  palette (`apply_palette_to_pixels`, dither None, the editing alpha threshold);
  `applyFilter` deliberately stays advisory, matching interactive filters.
- **Text layers go through the real pipeline.** `addTextLayer` and the `text` setter
  drive actual inline-editor sessions (the `cli_append_text_to_text_layers` technique),
  so rasters render through the normal commit path. `addTextLayer` clears the active
  layer first so `add_text_at` cannot latch onto an existing text layer.
- **Blend mode ids** (`script_blend_mode_id`) are a compatibility contract: scripts in
  the wild hard-code them. Append-only, aligned with the BlendMode enum, never rename.
- **`app.apiVersion` is 1.** Bump it only for breaking API changes, and record what
  changed here. July 2026 additions (all additive, still 1): `include()` search roots,
  `patchy.isMainScript()`, `patchy.args`, `patchy.ui.showDialog`, `patchy.io.listFiles`,
  `app.chooseFolder/chooseOpenFile/chooseSaveFile`, `app.runCommand/commandIds`.
- **`include()` resolution order**: relative to the including script, then the user
  scripts root, then the bundled scripts root; a result inside the bundled folder maps
  through the shadow-override store. `patchy.isMainScript()` is false during an included
  file's top-level code (include-depth counter), so a script can be both a library and
  runnable (`Effects/fancy-background.js` is the model; Breakout include()s it).
- **Modal helpers pause the watchdog.** Every interactive helper that blocks in a modal
  (alert, prompt, the pickers, `showDialog`, `runCommand`) wraps itself in
  `ModalWatchdogPause`, which disarms the watchdog and re-arms it with a FRESH timeout on
  exit - otherwise a user thinking at a dialog longer than the timeout gets the script
  interrupted the moment it resumes. New modal helpers must do the same.
- **`app.runCommand(id)`** triggers registered QActions by their stable HotkeyRegistry
  command id (the same ids the hotkey editor persists); returns false for unknown or
  disabled commands. It rides the same trust model as the rest of scripting.
- The script canvas window deliberately bypasses `run_non_modal_dialog` (that helper
  parks the caller in a nested event loop until the dialog finishes, and the calling
  script must keep running). It applies `keep_dialog_above_parent_window` directly, which
  is the macOS-critical part of the rule. The editor dialog itself is opened through
  `run_non_modal_dialog` as usual.

## CLI and AI control

```
patchy --run-script <file.js> [--script-output <out.txt>] [--script-arg key=value ...] [files...]
```

`--script-arg key=value` (repeatable) surfaces as `patchy.args.key` in the script (all
string values); the forwarded single-instance payload carries the raw tokens as extra
newline-separated fields after the output path, so keys and values must not contain
newlines. The bundled `Utilities/batch-export.js` is the reference consumer.

- With a running instance: the request forwards over the single-instance socket (the
  `patchy-cmd:run-script` reserved entry, same scheme as `--screenshot`), the invoker
  exits immediately, and the running instance executes the script. Console output,
  errors, and a final `[done]` or `[failed]` line are written to the output file when the
  run fully completes; the caller polls for the file. Warnings are prefixed `[warn] `,
  errors `[error] `, plain log lines are unprefixed so scripts can emit clean data (JSON
  included).
- Without one: a new instance runs unattended (`cli_automation_mode_`: prompts are
  suppressed, `app.alert` logs, `app.prompt` returns its default), opens any positional
  files first, writes the output file, and exits 0 on success or 4 on script error
  (2 and 3 belong to `--export`).
- A script that keeps timers or windows alive writes its output when the last one ends,
  so automation scripts should not open windows.

An AI agent drives Patchy by writing a .js file, invoking `--run-script`, and polling the
output file. `scripts/patchy.d.ts` is the machine-readable API description; point the
agent at it.

## Trust model

Scripts run with the application's privileges, like Photoshop or Affinity scripts: the
sandbox is "only run scripts you trust", not a permission system. The engine exposes no
file, network, or process API beyond the documented `patchy.io` text-file helpers and
document save/export paths, and v1 binds no network access at all. The single-instance
pipe is per-user, so `--run-script` adds no cross-user surface.

## Legal posture

- The API is Patchy's own design: generic OO naming, no Adobe ExtendScript identifiers,
  no cloned DOM, no copied documentation text. Keep it that way.
- Qt6Qml is LGPL and dynamically linked, the same posture as every other shipped Qt
  module. No vendored JS engine.

## Testing

- `tests/ui/scripting_tests.cpp` (`.\patchy_ui_visual_tests.exe ui_script`): mutations +
  single undo entry, stale-wrapper errors, pixel roundtrip + palette snap, timers,
  watchdog (via `PATCHY_SCRIPT_TIMEOUT_MS`), console/error line numbers, the CLI output
  file, the editor dialog, the canvas window, and the Scripts menu scan.
- The engine works offscreen; `ScriptEngineHost::message_backlog()` is the easiest
  assertion surface (fresh per MainWindow).
- Manual smoke: the bundled scripts all run from File > Scripts; `letter-physics.js`,
  `game-of-life.js`, `generative-art.js`, and `fancy-background.js` also complete under
  `--run-script` unattended.

## Future work (ranked by community research, July 2026)

A two-agent sweep of the Photoshop/Affinity/Krita/Aseprite/GIMP scripting communities
(Affinity's 35-page scripting mega-thread, Adobe UXP threads, Aseprite's itch.io script
economy, Krita Artists) ranked what artists actually use scripting for. Already covered:
batch folder processing (pickers + listFiles + batch-export.js), export-layers options,
form dialogs, CLI args, one-undo-per-run (Krita users beg for this), modern JS, typed API
docs. Deliberately NOT built yet, in rough demand order:

1. Events/hooks (document changed, before/after save/command) - on-save auto-export is
   the killer use. Needs a reentrancy design against the one-run-at-a-time rule.
2. Per-script keyboard shortcuts (Krita ships "Ten Scripts" purely for this). Would key
   HotkeyRegistry ids off the script's relative path; ids persist, so pick a stable
   scheme before shipping.
3. Data-merge/template-fill bundled script (CSV/JSON + text layers): already possible
   with today's API, just needs writing.
4. Persistent per-script storage (Aseprite `plugin.preferences`).
5. Tool-stroke API (draw as-if-by-hand with the real brush engine).
6. Macro-record-to-script (Photoshop's ScriptListener is how non-programmers start).
7. Non-blocking long batches (yield/progress helper), script packaging/sharing format,
   editor REPL mode.

Anti-goals from the same research: never freeze or fork the API surface (ExtendScript/UXP
split), no undocumented escape hatches as the real API (batchPlay), scripts stay plain
user-editable files (Affinity v3.2 shipped AI-generated-only scripting and the community
immediately built its own script manager).
