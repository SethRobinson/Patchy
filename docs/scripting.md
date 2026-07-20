# JavaScript scripting

Patchy embeds a JavaScript engine (Qt's QJSEngine, from the Qt6Qml module) with its own
document automation API, a Script Manager dialog (window title renamed from "Script
Editor" in July 2026; the `file.scripts.editor` command id and `scriptEditor*` object
names keep the historical spelling - persisted identifiers are never renamed), bundled
example scripts, and a CLI entry point that lets external tools and AI agents drive a
running Patchy. This doc is the authoritative record of the design rules; the
user-facing docs are `scripts/bundled/scripting-guide.md` (the human-readable guide -
README links it, and it renders in-app via the markdown viewer from Help > Scripting
Guide and the Script Manager's Help button) and `scripts/bundled/patchy.d.ts`
(TypeScript definitions). BOTH must track every API change - keeping the guide current
is part of the same rule that keeps patchy.d.ts current.

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
  Script Manager UI (folder tree, shadow-override saves, context menu, and the run
  status area: a spinner + "Running... 13s" elapsed readout with a stop-sign button
  while a run is active, "Ready" otherwise; the elapsed clock is dialog-local and
  restarts when run_state_changed reports a run became active). The C:\ toolbar button
  and the "Command Line Example..." context entry pop the copyable-command dialog
  (below), and the Help button opens the scripting guide.
- `src/ui/markdown_viewer_dialog.{hpp,cpp}`: the reusable read-only Markdown viewer
  (QTextBrowser's native Markdown rendering via setSource; relative images resolve
  against the .md file, anchors are repainted in the accent blue because the importer
  uses the palette's unreadable-on-dark default). `MainWindow::open_scripting_guide()`
  (main_window_scripting.cpp) owns the single instance, shared by Help > Scripting
  Guide (`help.scripting_guide`) and the Script Manager's Help button, and loads
  `scripting-guide.md` from the bundled scripts folder.
- `src/ui/script_folders.{hpp,cpp}`: the script browser model - recursive bundled/user
  folder scans and the shadow-override merge, shared by the File > Scripts menu and the
  editor tree.
- `src/ui/main_window_scripting.cpp`: the File > Scripts menu (subfolders become
  submenus), the editor entry, and the CLI flows (`run_script_command`, `run_cli_script`).
- `scripts/bundled/` (repo): bundled scripts + `patchy.d.ts` + `scripting-guide.md`,
  staged next to the binaries by the `patchy_bundled_scripts` copy-once target (macOS:
  `Contents/Resources/scripts`,
  Linux install: `share/patchy/scripts`). ONLY `scripts/bundled` ships - the rest of
  `scripts/` is dev tooling and must never be staged (the copy step cleans the staged
  folder first, so renames/removals propagate to existing build trees). Bundled scripts
  are organized into `Games/`, `Demos/`, `Effects/`, `Utilities/` (display names go
  through `script_folder_display_name` for localization). Bundled-script convention:
  only `Games/` scripts create their own document or window; every other bundled script
  works on the ACTIVE document and alerts "Open a document first." when none is open
  (mixing the two confuses users about where a script's output went). One carve-out: a
  Utilities script may create a document when that document IS its stated output
  (`contact-sheet.js` builds the sheet it is named after). Every bundled script must
  also finish cleanly unattended under `--run-script` (cancelled pickers return "",
  showDialog answers its defaults). User scripts live
  in the per-user app-data folder under `scripts/` (`MainWindow::user_scripts_directory()`).
- Tests: `tests/ui/scripting_tests.cpp`.

## Script metadata and icons

Header directives live in the `//` comment block at the top of a script and are read by
`read_script_metadata` (script_folders.cpp; parsing stops at the first non-comment line,
30 lines max):

- `// @name Breakout` - display name shown in the Script Manager tree and the
  File > Scripts menu (falls back to the file base name). Files sort by display name.
- `// @description ...` - the hover-card blurb; repeated `@description` lines join with
  a space.
- `// @author ...` - the hover-card credit line.
- `// @window` - the script creates its own window or document. Rendered as a small
  window badge; scripts without it work on the active document. Set on the three Games
  plus contact-sheet.js (it builds a new document).
- `// @cli ...` - the argument part of the script's command-line example: everything
  after `--run-script <script>`, verbatim (repeated lines join with a space, like
  `@description`). Consumed by `script_cli_example_command` (script_folders.cpp), which
  builds the copyable command the Script Manager's C:\ toolbar button and "Command Line
  Example..." context entry show: quoted exe path + `--run-script` + quoted script path
  + the `@cli` tokens. Without `@cli` the fallback appends an ` example.png` positional
  placeholder for active-document scripts and nothing for `@window` scripts, so every
  script gets a working example even with no metadata at all. The Utilities scripts
  that take `--script-arg` options carry `@cli` lines; simple active-document effects
  rely on the fallback.

A 128x128 PNG next to the script with the same base name (`Games/breakout.js` ->
`Games/breakout.png`) is its icon (big enough for the hover card; the tree scales it to
32); entries without one get the generic code-drawn JS-page icon
(`script_generic_icon`). The shadow-override rule extends to icons: a user PNG at the
same relative path overrides a bundled icon ON ITS OWN, independent of whether the .js
is overridden - that is where the Script Manager's "Set Icon from Current Window"
context command writes (`script_icon_write_target` + `write_script_icon`: capture,
center-crop square, scale to 128). The capture source is the most recently opened live
script canvas window, else the active document composite. The tree rows are painted by
a cpp-local QStyledItemDelegate in script_editor_dialog.cpp (two lines: name over
filename, amber "modified" tag, window badge); `item->text(0)` stays the display name.
Resting the pointer on a script row (~350 ms) pops the ScriptHoverCard (same file): the
128 icon at 96 px, name, "by author", the wrapped description, and a filename/badges
footer. The card is a slot-driven Qt::ToolTip window; the tree refresh clears the
hover state (items are deleted), and script rows deliberately have NO plain tooltip.

Every bundled script carries `@name`/`@description`/`@author`, and every one has a
committed icon PNG. The icons are procedural mini-artwork generated by
`scripts/dev/make-script-icons.js` (dev tooling, never staged) via `--run-script` -
code-generated per the repo's bundled-art rule, and regenerable; icons are DESIGNED in
64-unit space and rendered at 128 (every primitive scales by SCALE internally), and the
Fancy Background icon is that script's real output. After adding a bundled script, add
its directives, extend the generator, and re-run it (the header comment shows the
command line).

## Script options (the OPTIONS block + showOptions pattern)

Every bundled script with tweakable behavior follows one shape, and new scripts should
too:

1. A clearly-marked `var OPTIONS = {...}` block at the top of the file holds the
   defaults with one comment per key - the "easy to change variables" surface for
   artists editing the script.
2. The script calls `patchy.ui.showOptions({title, description, fields})` with the
   fields seeded from OPTIONS. `description` renders as wrapped instructions above the
   form - scripts that need input (contact-sheet, batch-export, data-merge) explain
   what to pick instead of throwing a bare file picker at the user.
3. showOptions implements "defaults unless overridden": matching `--script-arg
   key=value` tokens override the field defaults (coerced by field type; a bare token
   turns a checkbox on), unattended runs return the effective values WITHOUT a dialog,
   and GUI runs show the dialog seeded with them (null = cancelled, exit quietly).

"Unattended" is `ScriptEngineHost::unattended_run()`: app-wide CLI automation mode OR
the per-run `RunOptions.unattended` flag, which `run_script_command`/`run_cli_script`
set for every `--run-script` execution - INCLUDING requests forwarded to a running GUI
instance, so automation never blocks on a dialog. All the interactive helpers (alert,
prompt, pickers, showDialog, showOptions) honor it.

The form dialog (shared by showDialog/showOptions, `run_form_dialog` in
script_engine.cpp) also supports `folder` and `file` field types (path line edit +
Browse; values travel as "/" paths) and the `description` header. Games deliberately
show no dialog (OPTIONS block only - a game should just start); trim-to-content and
save-version stay instant too.

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
- **The watchdog measures INACTIVITY, never total runtime.** Legitimate scripts run for
  hours (contact sheets, batch converts); a blanket runtime limit is wrong by design.
  A helper thread arms around every evaluate and callback, and every hot service call
  feeds it (a lock-free atomic in `pump_progress_indicator`); it calls
  `QJSEngine::setInterrupted` only when a script made NO API call - no pixel write,
  file operation, or console output - for the whole window (default 2 minutes;
  `PATCHY_SCRIPT_TIMEOUT_MS` overrides the window, which the tests use). That is the
  only possible defense against `while (true) {}`: a frozen UI thread cannot show any
  prompt. Pure-JS computation that goes silent longer than the window still dies -
  the documented convention is to log or write progress periodically (every heavy
  bundled script does; the same calls drive the busy overlay). Never remove the
  arm/disarm pairing around a new entry point into script code; route new callback
  invocations through `call_script_callback`.
- **Reentrancy: never destroy the engine from inside script code.** Timer slots and
  window event filters run callbacks; a failing callback schedules a deferred finish
  (`schedule_completion_check` / the deferred `finish_run` path) instead of tearing down
  from inside itself. Stop during evaluation only interrupts; the evaluate caller
  finishes the run.
- **The automatic busy indicator pumps events mid-evaluation - keep the guards.**
  `pump_progress_indicator()` (called at the hot service entry points: prepare_mutation,
  note_*_changed, open/create/save/close session, apply_filter, add_text_layer,
  consoleEmit; it also feeds the watchdog, unconditionally) engages once the CURRENT
  synchronous burst - the main evaluate or one callback, measured by `burst_clock`,
  restarted per burst so a game of short frames never trips it - exceeds 500 ms
  (`PATCHY_SCRIPT_BUSY_DELAY_MS` overrides; skipped for unattended runs): the active
  canvas's processing overlay (when a canvas exists) PLUS the application-modal
  `ScriptStopPanel` (script name + elapsed + its last console line, refreshed per
  pump), then pumps `processEvents(AllEvents)` - the modality gate means the panel's
  Stop button is the only reachable control while the script owns the UI thread.
  Stop opens a NON-BLOCKING confirm ("Stop 'name'?" with an "Undo the changes it made"
  checkbox, shown only when the run pushed undo snapshots) - the job keeps working
  while the user decides (never exec a nested loop from the panel's handler: a
  timer-driven click could not be answered from its own nested loop, and an hours-long
  batch should not sit paused under a question). Cancel just dismisses it; confirming
  interrupts the run and `finish_run` undoes the snapshot in every touched session,
  closing the confirm automatically when the run ends on its own first.
  The invariants that make the pumping safe: the script timer slot defers when
  `sync_running || in_callback` (single-shots re-arm via `start(0)`) because QJSEngine
  is not reentrant; `end_progress_indicator()` closes overlay+panel when the burst,
  the session (close_session), or the run ends; and `ModalWatchdogPause` ends the
  indicator on entry (the panel must not fight the script's own dialogs) and restarts
  the burst clock on exit. Side effect: the pump runs the coalesced refresh flush, so
  scripts that push pixels repeatedly (generative-art batches, fancy-background
  chunks) paint progressively. A pure-JS loop with no API calls cannot pump - heavy
  bundled scripts write their buffer to the layer a few times mid-computation for
  exactly this reason (setPixels REPLACES the layer's pixels, so they re-send the
  whole buffer, never partial strips).
- **Palette mode**: `setPixels` and `fill` are tool-like writes and snap to the document
  palette (`apply_palette_to_pixels`, dither None, the editing alpha threshold);
  `applyFilter` deliberately stays advisory, matching interactive filters.
- **Text layers go through the real pipeline.** `addTextLayer` and the `text` setter
  drive actual inline-editor sessions (the `cli_append_text_to_text_layers` technique),
  so rasters render through the normal commit path. `addTextLayer` clears the active
  layer first so `add_text_at` cannot latch onto an existing text layer. Its `size` is
  the text height in DOCUMENT PIXELS: the inline editor's font lives in editor pixels
  (document px * canvas zoom), so the script path must set `setPixelSize(size * zoom)` -
  a point-sized font commits at a zoom-dependent size (the July 2026 dialog-showcase
  overlap bug; pinned by `ui_script_text_size_is_zoom_independent`).
- **Blend mode ids** (`script_blend_mode_id`) are a compatibility contract: scripts in
  the wild hard-code them. Append-only, aligned with the BlendMode enum, never rename.
- **`app.apiVersion` is 1.** Bump it only for breaking API changes, and record what
  changed here. July 2026 additions (all additive, still 1): `include()` search roots,
  `patchy.isMainScript()`, `patchy.args`, `patchy.ui.showDialog`, `patchy.io.listFiles`,
  `app.chooseFolder/chooseOpenFile/chooseSaveFile`, `app.runCommand/commandIds`,
  `getPixels` reading 8-bit RGB layers (opaque opened photos) expanded to RGBA with
  alpha 255 (it previously threw; `setPixels` still always writes RGBA8 back),
  `patchy.ui.showOptions`, the `folder`/`file` form field types, and the form dialogs'
  `description` header. Behavioral fix (still 1): `addTextLayer`'s `size` is defined as
  document pixels; it previously committed at a canvas-zoom-dependent size.
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
  included). Forwarded runs are marked `RunOptions.unattended`, so the interactive
  helpers answer with defaults instead of parking the GUI instance at a dialog.
- Without one: a new instance runs unattended (`cli_automation_mode_`: prompts are
  suppressed, `app.alert` logs, `app.prompt` returns its default), opens any positional
  files first, writes the output file, and exits 0 on success or 4 on script error
  (2 and 3 belong to `--export`).
- A script that keeps timers or windows alive writes its output when the last one ends,
  so automation scripts should not open windows.

An AI agent drives Patchy by writing a .js file, invoking `--run-script`, and polling the
output file. `scripts/patchy.d.ts` is the machine-readable API description and
`scripts/bundled/scripting-guide.md` the prose guide; point the agent at both.

The Script Manager's C:\ button surfaces this whole flow to users: it shows a copyable,
really-runnable command for the selected script (tree selection first, else the loaded
file), built by `script_cli_example_command` from the live application path and the
script's `@cli` directive. The metadata is re-read from disk on every click (never
cached), and the dialog is opened with `open()` (window-modal, no nested event loop).
Shell rule (learned the hard way - a pasted command MUST run, footnotes are not read):
the exe token stays unquoted whenever the path is plain, because that one form runs
as pasted in Command Prompt, PowerShell, and batch files, while quoting the first
token flips PowerShell into expression mode ("Unexpected token" on `--run-script`).
When the path forces quotes (spaces - Program Files installs), the shells genuinely
diverge (PowerShell needs the `& ` call operator, cmd rejects it), so the dialog shows
TWO labeled copyable lines, one per shell, each with its own Copy button.

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
  file, the editor dialog, the canvas window, the Scripts menu scan, the `@cli`
  directive + example-command builder + C:\ dialog (clipboard included), and the
  scripting-guide viewer (both Help entry points, single shared instance).
- The engine works offscreen; `ScriptEngineHost::message_backlog()` is the easiest
  assertion surface (fresh per MainWindow).
- Manual smoke: the bundled scripts all run from File > Scripts; `game-of-life.js`
  completes fully under `--run-script` unattended, and the active-document scripts
  (`letter-physics.js`, `generative-art.js`, `fancy-background.js`, the Effects and
  the document-based Utilities) run unattended against a positional file (with no
  document they alert-and-finish clean; picker-driven scripts like `batch-export.js`,
  `contact-sheet.js`, and `data-merge.js` take their folders/files via `--script-arg`
  and cancel cleanly without them).

## Future work (ranked by community research, July 2026)

A two-agent sweep of the Photoshop/Affinity/Krita/Aseprite/GIMP scripting communities
(Affinity's 35-page scripting mega-thread, Adobe UXP threads, Aseprite's itch.io script
economy, Krita Artists) ranked what artists actually use scripting for. Already covered:
batch folder processing (pickers + listFiles + batch-export.js), export-layers options,
form dialogs, CLI args, one-undo-per-run (Krita users beg for this), modern JS, typed API
docs, and (July 2026) the ten-script batch: data merge (CSV + text layers - was item 3
of this list), contact sheets, icon export, versioned saves, batch layer rename, grid
overlays, duotone/glitch/photo-frame effects, and a form-dialog showcase demo.
Deliberately NOT built yet, in rough demand order:

1. Events/hooks (document changed, before/after save/command) - on-save auto-export is
   the killer use. Needs a reentrancy design against the one-run-at-a-time rule.
2. Per-script keyboard shortcuts (Krita ships "Ten Scripts" purely for this). Would key
   HotkeyRegistry ids off the script's relative path; ids persist, so pick a stable
   scheme before shipping.
3. Persistent per-script storage (Aseprite `plugin.preferences`).
4. Tool-stroke API (draw as-if-by-hand with the real brush engine).
5. Macro-record-to-script (Photoshop's ScriptListener is how non-programmers start).
6. Non-blocking long batches (yield/progress helper), script packaging/sharing format,
   editor REPL mode.

Anti-goals from the same research: never freeze or fork the API surface (ExtendScript/UXP
split), no undocumented escape hatches as the real API (batchPlay), scripts stay plain
user-editable files (Affinity v3.2 shipped AI-generated-only scripting and the community
immediately built its own script manager).
