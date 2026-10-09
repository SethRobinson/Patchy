# JavaScript scripting

Patchy embeds a JavaScript engine (Qt's QJSEngine, from the Qt6Qml module) with its own
document automation API, a Script Manager dialog (the `file.scripts.editor` command id
and `scriptEditor*` object names keep the old "Script Editor" spelling; persisted
identifiers are never renamed), bundled
example scripts, and a CLI entry point that lets external tools and AI agents drive a
running Patchy. This doc is the authoritative record of the design rules; the
user-facing docs are `scripts/bundled/scripting-guide.md` (the human-readable guide,
linked from the README and rendered in-app from Help > Scripting Guide) and
`scripts/bundled/patchy.d.ts` (TypeScript definitions). BOTH must track every API
change.

## Where things live

- `src/ui/script_engine.{hpp,cpp}`: `ScriptEngineHost`, the engine owner. Run lifecycle,
  bootstrap prelude (console/timers/include/the `patchy` namespace), watchdog, undo and
  refresh integration, and every MainWindow-facing service the wrappers call (the
  interactive helpers included). It is a friend of MainWindow; the wrappers never touch
  MainWindow directly.
- `src/ui/script_api.{hpp,cpp}`: the QObject wrappers JS sees (`app`, documents, layers,
  selection, `patchy.io`, `patchy.ui`, `patchy.recovery`, `patchy.plugins`; the last one
  and `layer.applyPlugin` front the legacy 8BF host, see [plugins.md](plugins.md)).
- `src/ui/script_vector*.{hpp,cpp}`: native shapes, paints, paths, masks,
  organization, and selection bindings. See [vector-automation.md](vector-automation.md)
  for shared operations, validation, and refresh contracts.
- `ui/script_brush` and `ui/brush_automation`: brush bindings, resolution and
  presets; see [brush-automation.md](brush-automation.md).
- `src/ui/script_canvas_window.{hpp,cpp}`: interactive script windows (games/demos).
- `src/ui/script_editor_dialog.{hpp,cpp}` + `src/ui/js_syntax_highlighter.{hpp,cpp}`: the
  Script Manager UI (folder tree, shadow-override saves, context menu, run status area).
  The C:\ button and the "Command Line Example..." entry open the copyable-command dialog
  (below). A single click loads a tree script; with unsaved edits, selection changes never
  load or prompt, and switching goes through activation (double-click/Enter), which asks
  to discard. New seeds a runnable starter template, left unmodified so an untouched
  template never guards or prompts.
- `src/ui/markdown_viewer_dialog.{hpp,cpp}`: the read-only Markdown viewer
  (QTextBrowser via setSource; relative images resolve against the .md file, anchors
  are repainted in the accent blue because the importer's default is unreadable on
  dark). `MainWindow::open_scripting_guide()` owns the single instance shared by Help >
  Scripting Guide (`help.scripting_guide`) and the Script Manager's Help button. Help > Set
  up AI Control (`help.ai_setup`) is the MCP connector's sibling; see [ai-control.md](ai-control.md).
- `src/ui/script_folders.{hpp,cpp}`: the script browser model (recursive bundled/user
  scans, the shadow-override merge), shared by the Scripts menu and the editor tree.
- `src/ui/main_window_scripting.cpp`: the File > Scripts menu, the script commands, and
  the CLI flows (`run_script_command`, `run_cli_script`).
- `scripts/bundled/` (repo): bundled scripts + `patchy.d.ts` + `scripting-guide.md`,
  staged next to the binaries by the `patchy_bundled_scripts` copy-once target (macOS:
  `Contents/Resources/scripts`, Linux install: `share/patchy/scripts`). ONLY `scripts/bundled` ships - the rest of
  `scripts/` is dev tooling and must never be staged (the copy step cleans the staged
  folder first, so renames/removals propagate to existing build trees; the macOS bundle
  POST_BUILD does the same rm-rf-then-copy into Resources/fonts, /translations, and
  /scripts). Bundled scripts
  are organized into `Games/`, `Demos/`, `Effects/`, `Utilities/` (display names go
  through `script_folder_display_name` for localization). Bundled-script convention:
  only `Games/` scripts create their own document or window; every other bundled script
  works on the ACTIVE document and alerts "Open a document first." when none is open.
  One carve-out: a Utilities script may create a document when that document IS its
  stated output (`contact-sheet.js`). Every bundled script must
  also finish cleanly unattended under `--run-script` (cancelled pickers return "",
  showDialog answers its defaults). User scripts live
  in the per-user app-data folder under `scripts/` (`MainWindow::user_scripts_directory()`).
- Tests: `tests/ui/scripting_tests.cpp`.

## Script metadata and icons

Header directives live in the `//` comment block at the top of a script, read by
`read_script_metadata` (script_folders.cpp; parsing stops at the first non-comment line,
30 lines max):

- `// @name Breakout` - display name shown in the Script Manager tree and the
  File > Scripts menu (falls back to the file base name). Files sort by display name.
- `// @description ...` - the hover-card blurb; repeated `@description` lines join with
  a space.
- `// @author ...` - the hover-card credit line.
- `// @window` - the script creates its own window or document. Rendered as a small
  window badge; scripts without it work on the active document. Set on the three Games
  plus contact-sheet.js.
- `// @cli ...` - the argument part of the script's command-line example: everything
  after `--run-script <script>`, verbatim (repeated lines join with a space, like
  `@description`). Consumed by `script_cli_example_command` (script_folders.cpp), which
  builds the copyable command: exe path + `--run-script` + quoted script path + the
  `@cli` tokens. Without `@cli` the fallback appends an ` example.png` positional
  placeholder for active-document scripts and nothing for `@window` scripts, so every
  script gets a working example. Utilities scripts that take `--script-arg` options
  carry `@cli` lines; simple active-document effects rely on the fallback.
- `// @hotkey Ctrl+Alt+D` - the script's DEFAULT shortcut (one PortableText sequence; a
  key `is_reserved_binding_key` refuses is ignored); see "Script hotkeys". Bundled scripts
  never carry one (`ui_hotkey_defaults_have_no_conflicts` registers them all).

A same-stem 128x128 PNG is the script icon (32px in the tree, 96px in the hover card;
missing icons use `script_generic_icon`). User PNGs override bundled icons independently
of script overrides. "Set Icon from Current Window" (`script_icon_write_target` /
`write_script_icon`) center-crops the latest live script canvas or the active document.
The tree delegate paints name, filename, modified tag and window badge; `item->text(0)`
remains the display name. The slot-driven `ScriptHoverCard` shows icon, name, author,
description and filename/badges after about 350ms; tree refresh clears hover state, and
rows have no plain tooltip.

Every bundled script carries `@name`/`@description`/`@author`, and every one has a
committed icon PNG, procedural artwork generated by `scripts/dev/make-script-icons.js`
(dev tooling, never staged) via `--run-script`. After adding a bundled script, add its
directives, extend the generator, and re-run it (its header comment shows the command).

## Script options (OPTIONS block + showOptions)

Every bundled script with tweakable behavior follows one shape; new scripts should too:

1. A clearly-marked `var OPTIONS = {...}` block at the top holds the defaults, one
   comment per key.
2. The script calls `patchy.ui.showOptions({title, description, fields})` with the
   fields seeded from OPTIONS; `description` renders as instructions above the form.
3. showOptions implements "defaults unless overridden": matching `--script-arg
   key=value` tokens override the field defaults (coerced by field type; a bare token
   turns a checkbox on), unattended runs return the effective values WITHOUT a dialog,
   and GUI runs show the dialog seeded with them (null = cancelled, exit quietly).

"Unattended" is `ScriptEngineHost::unattended_run()`: app-wide CLI automation mode OR
the per-run `RunOptions.unattended` flag, set for every `--run-script` execution
INCLUDING requests forwarded to a running GUI instance, so automation never blocks on a
dialog. Every interactive helper honors it.

The form dialog (shared by showDialog/showOptions, `run_form_dialog` in
script_engine.cpp) also supports `folder` and `file` field types (path line edit +
Browse; values travel as "/" paths) and the `description` header. Games deliberately
show no dialog (OPTIONS block only - a game should just start); trim-to-content and
save-version stay instant too.

## Shadow overrides (saving over a bundled script)

Save on a bundled script never touches the shipped file (read-only installs; app updates
would clobber edits): the editor writes a user copy at the SAME relative path under the
user scripts folder. The scan merge (`scan_scripts`) shows that copy in place of the
bundled entry, tagged "(modified)", and it is what runs from the menu, the editor, and
`include()`. "My Scripts" lists only non-overriding user scripts; Revert to Bundled
deletes the copy. Keep this override-by-relative-path rule intact everywhere a bundled
script is resolved.

## Engine rules (binding design decisions)

- **One run at a time, on the UI thread.** A run stays alive until the synchronous
  evaluation AND every timer and script canvas window are done. A fresh QJSEngine per
  run means stored QJSValues die with their engine (canvas windows drop theirs first).
- **One undo entry per run and session by default.** The first mutation a run makes to a
  session pushes one "Script: name" snapshot (`prepare_mutation`); everything after rides
  it, so a 60fps animation undoes in one step. Scripts can opt out for
  speed with `app.undoEnabled = false` (per-run state, resets to true each run): the
  snapshot is skipped and those edits cannot be undone, but sessions are still marked
  modified so closing protects the work. Connector sessions
  reject disabling history so failed edits remain recoverable. `patchy.ui.slowMode`
  instead separates native strokes and undoable edits; see [automation-feedback.md](automation-feedback.md).
- **Wrappers hold ids, never pointers.** Layer wrappers keep session id + LayerId and
  re-resolve on every access, throwing a JS error when the target is gone (a stored
  `Layer*` is the historical use-after-free pattern). Reads resolve through const
  documents (mutable layer accessors bump revisions on access).
- **Refresh is coalesced.** Mutations mark per-session dirt; one deferred flush per
  event-loop turn repaints the canvas and refreshes panels for the active session only
  (structure changes rebuild the layer panel; pixel-only changes refresh thumbnails).
  Visible MCP/CLI runs also present completed edits periodically (`patchy.ui.present`
  gives explicit frames and pacing; CLI runs have a status-bar Stop control). See
  [automation-feedback.md](automation-feedback.md).
  `patchy.ui.paused` parks visible automation after a native edit; paused manual edits
  split script Undo groups (API scope and resume safety: automation-feedback.md).
- **The watchdog measures INACTIVITY, never total runtime.** Legitimate scripts run for
  hours; a blanket runtime limit is wrong by design. A helper thread arms around every
  evaluate and callback, every hot service call feeds it (a lock-free atomic in
  `pump_progress_indicator`), and it calls `QJSEngine::setInterrupted` only when a script
  made NO API call for the whole window (default 2 minutes; `PATCHY_SCRIPT_TIMEOUT_MS`
  overrides it, which the tests use): the only defense against `while (true) {}`, since
  a frozen UI thread cannot prompt. Silent pure-JS computation still dies, so heavy
  scripts log or write progress periodically. Connector-owned runs instead use a
  throttled UI progress callback under the MCP input guard (status-bar Stop, no modal
  panel). Never remove the arm/disarm pairing around a new entry point into script
  code; route new callback invocations through `call_script_callback`.
- **Reentrancy: never destroy the engine from inside script code.** A failing timer or
  window-event callback schedules a deferred finish (`schedule_completion_check`) instead
  of tearing down from inside itself. Stop during evaluation only interrupts; the
  evaluate caller finishes the run.
- **The automatic busy indicator pumps events mid-evaluation - keep the guards.**
  `pump_progress_indicator()` (called at the hot service entry points, and it feeds the
  watchdog unconditionally) engages once the CURRENT
  synchronous burst (the main evaluate or one callback, timed by `burst_clock`,
  restarted per burst) exceeds 500 ms (`PATCHY_SCRIPT_BUSY_DELAY_MS` overrides; skipped
  for unattended runs): the canvas processing overlay plus the application-modal
  `ScriptStopPanel`, then `processEvents(AllEvents)`; modality leaves the panel's Stop
  button as the only reachable control while the script owns the UI thread.
  Stop opens a NON-BLOCKING confirm (with an "Undo the changes it made" checkbox when
  the run pushed undo snapshots); the job keeps working while the user decides (never
  exec a nested loop from the panel's handler: a timer-driven click could not be answered
  from its own nested loop). Confirming interrupts the run and `finish_run` undoes the
  snapshot in every touched session; the confirm closes by itself when the run ends first.
  The invariants that make the pumping safe: the script timer slot defers when
  `sync_running || in_callback` (single-shots re-arm via `start(0)`) because QJSEngine
  is not reentrant; `end_progress_indicator()` closes overlay+panel when the burst,
  the session (close_session), or the run ends; and `ModalWatchdogPause` ends the
  indicator on entry (the panel must not fight the script's own dialogs) and restarts
  the burst clock on exit. `createCanvas` does the same through
  `dismiss_busy_indicator()`, and the panel is not raised at all while the run owns an
  open canvas window (`has_open_canvas_window`): a window created under an
  application-modal window is blocked by it and skipped by key delivery, permanently
  on wasm (docs/wasm.md).
  Side effect: the pump runs the coalesced refresh flush, so scripts that push pixels
  repeatedly paint progressively. A pure-JS loop with no API calls cannot pump, so heavy
  bundled scripts write their buffer to the layer a few times mid-computation
  (setPixels REPLACES the layer's pixels: re-send the whole buffer, never strips).
- **Palette mode**: `setPixels` and `fill` snap to the document palette like tools
  (`apply_palette_to_pixels`, dither None, the editing alpha threshold); `applyFilter`
  stays advisory. `doc.getPalette/setPalette/loadPalette/savePalette` validate before
  Undo, keep layer pixels, and default to enabled mode; getters read const and return
  copies. PNG save/export uses the indexed writer; previews stay truecolor. Parallel
  `names` arrays carry color labels through GPL, PSD and indexed PNG. See
  [palette-mode.md](palette-mode.md).
- **Text layers go through the real pipeline.** `addTextLayer` and the `text` setter
  drive actual inline-editor sessions (the `cli_append_text_to_text_layers` technique),
  so rasters render through the normal commit path. `addTextLayer` clears the active
  layer first so `add_text_at` cannot latch onto an existing text layer. Its `size` is
  the text height in DOCUMENT PIXELS: the editor's font lives in editor pixels, so the
  script path sets `setPixelSize(size * zoom)` (`ui_script_text_size_is_zoom_independent`).
  Its `font` goes through `apply_text_family_to_editor`: the commit reads the session
  family, not the char format (`ui_script_text_font_option_applies`). The `text` setter
  replaces the selection in one `insertText(text, format)` with the first character's
  format, never delete-then-insert: an emptied block's char format is the fallback font
  only (`ui_la_methode_script_text_setter_matches_interactive_commit_if_available`).
  Rich runs (`addTextLayer([{text, font, size, bold, italic, color}, ...])`,
  `layer.setTextRuns`) type each run with its own `QTextCharFormat` through
  `apply_text_run_to_format`; a paragraph break serializes as its own run because Qt
  gives the block separator the preceding text's format. `box: {width, height}` opens the
  session as paragraph text; `align`/`textAlign` go through `apply_text_alignment_to_editor`.
  `textRuns`, `textBox` (null unless the flow metadata says box) and `textAlign` read the
  stored metadata without a session. Scripted layers clear `kTextStyleNameFormatProperty`
  and carry `kTextExactSizeFormatProperty`, so the requested face and size commit at every
  zoom (`ui_script_text_face_ignores_the_options_bar_style`,
  `ui_script_text_size_survives_low_zoom_reedit`, `ui_script_text_runs_create_and_read_back`,
  `ui_script_text_box_wraps_and_aligns`, `ui_script_set_text_runs_edits_existing_layer`).
- **A script move is a Move tool move.** `layer.moveTo` and the `x`/`y` setters shift
  the bounds, then call `translate_moved_layer_metadata` on the layer and every
  descendant: the linked mask, shape model, linked vector mask, smart-object quads and
  text transform (Photoshop's text anchor) travel with the pixels, and unlinked masks
  stay. Never shift a mask by hand beside that helper (it moves twice). A supported
  Smart Filter stack re-renders at the new place (`rerender_moved_smart_filters`); a
  failed render restores the document and throws. `alignLayers`/`distributeLayers`
  (`CanvasWidget::offset_layers`) and `duplicate(targetDocument)`
  (`offset_copied_layer_tree`) follow the same rule. A group has empty bounds: `x`/`y`
  read 0 and `moveTo` offsets its contents. Pinned by `ui_script_move_*`; Photoshop
  check: `scripts\dev\photoshop-text-move-check.ps1`.
- **Blend mode ids** (`script_blend_mode_id`) are a compatibility contract: append-only,
  aligned with the BlendMode enum, never renamed.
- **`app.apiVersion` is 1.** Bump only for breaking changes. Record additions and
  behavioral changes in [scripting-api-changes.md](scripting-api-changes.md).
  The MCP connector APIs (stable-ID lookups, strokes, previews, history access,
  structured results) are additive; their contracts are in [ai-control.md](ai-control.md).
- **`include()` resolution order**: relative to the including script, then the user
  root, then the bundled root; a bundled result maps through the shadow-override store. `patchy.isMainScript()` is false during an included
  file's top-level code (include-depth counter), so a script can be both a library and
  runnable (`Effects/fancy-background.js` is the model).
  include() saves and restores the includer's global `OPTIONS` binding around the nested
  evaluation (every bundled script has one; a library's block used to overwrite it).
- **Sound effects (`patchy.ui.playTone`/`playSound`)** play through
  `src/ui/sound_effects.{hpp,cpp}` + `sound_effects_mac.mm`: a deterministic 16-bit
  mono tone synth and per-OS fire-and-forget playback (winmm, `NSSound`, a detached
  `paplay`/`pw-play`/`aplay` process; none installed = silent no-op). Deliberately NOT
  Qt Multimedia (absent from the vendored Qt; packaging everywhere would grow). `playSound` resolves relative paths the
  include() way, requires RIFF/WAVE, and throws for missing/invalid files.
  `PATCHY_NO_SOUND=1` validates but skips the OS call (how offscreen tests stay silent).
- **Modal helpers pause the watchdog.** Every interactive helper that blocks in a modal
  wraps itself in `ModalWatchdogPause` (disarms, re-arms with a FRESH timeout on exit, so
  time spent at a dialog never interrupts the script). New modal helpers must do the same.
- **`app.runCommand(id)`** triggers registered QActions by their HotkeyRegistry command
  id; false for unknown, disabled, or `script.` commands.
- **`patchy.ui.zoom` / `patchy.ui.fitOnScreen()`** are the view controls (percent, active
  document); fitting settles posted layout first. Window captures wait for
  [Dynamic Vector Preview](vector-preview.md).
- The script canvas window deliberately bypasses `run_non_modal_dialog` (its nested
  event loop would park the running script) and applies `keep_dialog_above_parent_window`
  directly, the macOS-critical part of the rule. The editor dialog uses
  `run_non_modal_dialog` as usual.

## Script hotkeys

Every script in the merged scan owns one persistent `QAction` (parent MainWindow) that is
both its File > Scripts entry and a HotkeyRegistry command of category `scripts`, so the
menu shows the shortcut and the key works from the first keypress.
`MainWindow::refresh_script_commands(const ScriptScan&)` (main_window_scripting.cpp) owns
them: registers new scripts, unregisters vanished ones (`HotkeyRegistry::unregister_command`),
re-defaults a changed `@hotkey` (`set_default_shortcuts`), refreshes text, icon and run
path, attaches new actions to every open float window (docs/float-windows.md), then calls
`apply_to_actions()` once. It is the ONLY sanctioned late registration and runs at
construction, on every Scripts menu rebuild and Script Manager tree refresh, and before
the Hotkeys panel is built. `rebuild_scripts_menu` re-adds the actions in scan order.

- **Command id** (persisted, never changes): `script_hotkey_command_id` is `"script."` plus
  the percent-encoded relative path (`script.Utilities%2Fquick-export-dds.js`; a `/` in a
  QSettings key would open a group). Unique across the merged scan, so a shadow override
  keeps the bundled script's hotkey (its own `@hotkey` becomes the default); a renamed or
  moved file gets a new id.
- **Precedence**: a Preferences override (group `hotkeys`) beats the `@hotkey` default;
  built-in defaults beat a colliding `@hotkey` (a suppression, noted on the row).
  `apply_overrides` drops overrides for unregistered ids: a deleted script's binding goes
  at the next Preferences OK.
- **Editor**: `build_rows` lists category `scripts` under "Scripts" by display name even
  when the menu walk would find them (the menu is built lazily). "Assign Hotkey..." calls
  `MainWindow::show_hotkey_preferences(display_name)`: the Hotkeys tab, search prefilled.
- **Right-click on a menu entry** (`kScriptsMenuProperty` menus, the recent-files event
  filter pattern) opens `MainWindow::show_script_context_menu`: the Script Manager's
  actions via its static helpers (`show_cli_example_dialog`, `write_icon_from_current_window`,
  `confirm_and_revert_override`) plus "Edit in Script Manager..." (`open_script`). Folder
  changes go through `MainWindow::rescan_scripts()` (scan, commands, an open tree).
- **`patchy.scripts`** (`ScriptLibraryObject`): `list`/`rescan`, `install(relativePath,
  source, {hotkey})` (QSaveFile below the user folder, path validated), `setHotkey`/
  `getHotkey` (the Preferences override via `apply_overrides`); how an MCP agent writes a
  script and binds its key in one run (agent-kit workflow.md).
- **Isolation**: `PATCHY_USER_SCRIPTS_DIR` overrides `user_scripts_directory()`; the UI
  suite uses `test-artifacts/user-scripts/<pid>` so a developer's scripts never register.
- `app.runCommand` refuses `script.` ids (one run at a time). Pinned by
  `ui_script_hotkey_runs_user_script`, `ui_script_hotkey_ids_follow_relative_path`,
  `ui_scripts_menu_lists_bundled_scripts`, `ui_hotkey_editor_lists_scripts_category`.

## Explicit save options

`doc.saveAs(path, options)` / `doc.exportAs(path, options)` (`ScriptDocumentObject::save_to_path`)
start from `MainWindow::image_save_defaults_for_document()`, overlay the object through
`apply_script_image_save_options` (src/ui/script_save_options.cpp), and call
`save_document_to_path` with `SaveToPathPolicy{flatten_confirmed, scripted, export_copy}`.
The parser is strict (unknown key, wrong type, out of range, or a key for another extension
throws before anything is written) and reuses the persisted tokens from
`image_save_option_keys.hpp`, shared with the dialogs and settings. No options object =
the old path (defaults or the session's remembered options). `scripted` skips the
saved-channels and Aseprite fill-opacity prompts and never persists the user's defaults or
save directory; `export_copy` (exportAs) takes the save-a-copy branch for every format, so
the session's path, title and modified state stay untouched. Pinned by
`ui_script_save_as_options_write_dds_and_jpeg` and `ui_script_save_as_options_are_strict`.

## CLI and AI control

`patchy-mcp` provides a persistent workspace over local stdio MCP: offscreen by
default, a separate window with `--visible`, or the user's open workspace with
`--attach` (mutations then need an expected-state token). It shares application
startup and the scripting engine with `patchy`, isolates window preferences, shares
saved brushes and recent history, and ships the `patchy-control` skill. Setup,
lifecycle, indicators, protocol and packaging are in [ai-control.md](ai-control.md).

```
patchy [--headless] --run-script <file.js> [--script-output <out.txt>] [--script-arg key=value ...] [files...]
```

`--script-arg key=value` (repeatable) surfaces as `patchy.args.key` in the script (all
string values); the forwarded single-instance payload carries the raw tokens as extra
newline-separated fields after the output path, so keys and values must not contain
newlines. The bundled `Utilities/batch-export.js` is the reference consumer.

- With a running instance: file/script requests wait until any current script
  finishes before dispatch, including Finder opens. The request forwards over the single-instance socket (the
  `patchy-cmd:run-script` reserved entry, same scheme as `--screenshot`), the invoker
  exits immediately, and the running instance executes the script. Console output,
  errors, and a final `[done]` or `[failed]` line are written to the output file when the
  run fully completes; the caller polls for the file. Warnings are prefixed `[warn] `,
  errors `[error] `, plain log lines are unprefixed so scripts can emit clean data (JSON
  included). Forwarded runs are unattended (above).
- Without one: a new instance runs unattended (`cli_automation_mode_`: prompts are
  suppressed, `app.alert` logs, `app.prompt` returns its default), opens positional files
  first, writes the output file, and exits 0 on success or 4 on script error (2 and 3
  belong to `--export`) through `exit_cli_application` (ui/cli_exit.hpp; on wasm it shuts
  the Emscripten runtime down so qtloader receives the code, docs/wasm.md).
- A script that keeps timers or windows alive writes its output when the last one ends,
  so automation scripts should not open windows.
- `--headless` (any CLI mode): `headless_flag_present` (`support/cli_flags.hpp`) scans
  raw argv before the QApplication exists and sets `QT_QPA_PLATFORM=offscreen` (beats an
  ambient value), `PATCHY_HEADLESS=1` (lets the Windows registry font rescue run; the
  offscreen suites never set it) and `PATCHY_NO_SOUND=1`; forces single-instance off,
  sets `cli_automation_mode_`, and exits 2 without a mode flag. Release packages ship the
  offscreen plugin and every packager smoke-tests it (docs/release-process.md).
- Successful unattended opens and saves (flat copies included) update recent files and
  folders; failed operations and preview captures do not. `PATCHY_SETTINGS_DIR` isolates
  this history for tests.

The Script Manager's C:\ button shows that command for the selected script (tree
selection first, else the loaded file), built by `script_cli_example_command`; metadata
is re-read on every click, and the dialog opens with `open()` (no nested loop).
Shell rule (a pasted command MUST run as pasted): the exe token stays unquoted whenever
the path is plain (runs in cmd, PowerShell and batch files; a quoted first token flips
PowerShell into expression mode). A path that forces quotes splits the shells
(PowerShell needs `& `, cmd rejects it), so the dialog shows TWO labeled lines, one per
shell, on Windows only; POSIX shells run a quoted first token.

## Trust model

Scripts run with the application's privileges, like Photoshop or Affinity scripts: the
sandbox is "only run scripts you trust", not a permission system. The engine exposes no
file, network, or process API beyond the documented `patchy.io` and `patchy.scripts`
helpers and document save/export paths. The single-instance pipe is per-user.

## Legal posture

- The API is Patchy's own design: generic OO naming, no Adobe ExtendScript identifiers,
  no cloned DOM, no copied documentation text. Keep it that way.
- Qt6Qml is LGPL and dynamically linked like every shipped Qt module; nothing vendored.

## Testing

- `tests/ui/scripting_tests.cpp` (`.\patchy_ui_visual_tests.exe ui_script`) covers the
  engine rules, the CLI output file, the Script Manager, the canvas window, the Scripts
  menu, save options, hotkeys, and `patchy.io` on Unicode paths; layer moves live in
  `tests/ui/script_move_tests.cpp`. The engine works offscreen;
  `ScriptEngineHost::message_backlog()` is the easiest assertion surface.
- Manual smoke: every bundled script runs from File > Scripts and finishes unattended
  under `--run-script` (picker-driven ones take `--script-arg`, and cancel cleanly without).

## Future work

Not built: document/save/command hooks with a reentrancy design, persistent script
storage, macro recording, non-blocking batches, script packaging, an editor REPL.
Anti-goals: never freeze or fork the API surface, no undocumented escape hatches, and
scripts stay plain user-editable files.

Unattended runs (forwarded ones included): RAW imports read `.rawprefs` sidecars or
defaults without writing settings, PDF imports use defaults, dialogs return Cancel,
modified documents stay open unless the script calls `doc.close()`, forms normalize
without showing, and menu Undo/Redo/Quit are rejected while the script owns the
transaction. The editor's syntax, gutter and hover-card colors use theme roles and
refresh on scheme changes.
