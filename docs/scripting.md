# JavaScript scripting

Patchy embeds a JavaScript engine (Qt's QJSEngine, from the Qt6Qml module) with its own
document automation API, a Script Editor dialog, bundled example scripts, and a CLI entry
point that lets external tools and AI agents drive a running Patchy. This doc is the
authoritative record of the design rules; the user-facing API reference is
`scripts/patchy.d.ts` (TypeScript definitions, shipped next to the bundled scripts).

## Where things live

- `src/ui/script_engine.{hpp,cpp}`: `ScriptEngineHost`, the engine owner. Run lifecycle,
  bootstrap prelude (console/timers/include/the `patchy` namespace), watchdog, undo and
  refresh integration, and every MainWindow-facing service the wrappers call.
  `ScriptEngineHost` is a friend of MainWindow; the wrappers never touch MainWindow
  directly.
- `src/ui/script_api.{hpp,cpp}`: the QObject wrappers JS sees (`app`, documents, layers,
  selection, `patchy.io`, `patchy.ui`).
- `src/ui/script_canvas_window.{hpp,cpp}`: interactive script windows (games/demos).
- `src/ui/script_editor_dialog.{hpp,cpp}` + `src/ui/js_syntax_highlighter.{hpp,cpp}`: the
  editor UI.
- `src/ui/main_window_scripting.cpp`: the File > Scripts menu, folder scan, the editor
  entry, and the CLI flows (`run_script_command`, `run_cli_script`).
- `scripts/` (repo root): bundled scripts + `patchy.d.ts`, staged next to the binaries by
  the `patchy_bundled_scripts` copy-once target (macOS: `Contents/Resources/scripts`,
  Linux install: `share/patchy/scripts`). User scripts live in the per-user app-data
  folder under `scripts/` (`MainWindow::user_scripts_directory()`).
- Tests: `tests/ui/scripting_tests.cpp`.

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
  changed here.
- The script canvas window deliberately bypasses `run_non_modal_dialog` (that helper
  parks the caller in a nested event loop until the dialog finishes, and the calling
  script must keep running). It applies `keep_dialog_above_parent_window` directly, which
  is the macOS-critical part of the rule. The editor dialog itself is opened through
  `run_non_modal_dialog` as usual.

## CLI and AI control

```
patchy --run-script <file.js> [--script-output <out.txt>] [files...]
```

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
  `game-of-life.js`, and `generative-art.js` also complete under
  `--run-script` unattended.
