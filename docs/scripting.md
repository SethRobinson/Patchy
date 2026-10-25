# JavaScript scripting

Patchy embeds a JavaScript engine (Qt's QJSEngine, from the Qt6Qml module) with its own
document automation API, a Script Manager dialog (the `file.scripts.editor` command id
and `scriptEditor*` object names keep the old "Script Editor" spelling; persisted
identifiers are never renamed), bundled example scripts, and a CLI entry point for
external tools and AI agents. This doc holds the design rules; the user-facing docs are
`scripts/bundled/scripting-guide.md` (linked from the README, rendered in-app from Help >
Scripting Guide, served to MCP clients as `get_help(guide)`) and
`scripts/bundled/patchy.d.ts` (TypeScript definitions). BOTH must track every API
change, and every change gets an entry in [scripting-api-changes.md](scripting-api-changes.md).

## Where things live

- `src/ui/script_engine.{hpp,cpp}`: `ScriptEngineHost`, the engine owner. Run lifecycle,
  bootstrap prelude (console/timers/include/the `patchy` namespace), watchdog, undo and
  refresh integration, and every MainWindow-facing service the wrappers call (the
  interactive helpers included). It is a friend of MainWindow; the wrappers never touch
  MainWindow directly.
- `src/ui/script_api.{hpp,cpp}`: the QObject wrappers JS sees (`app`, documents, layers,
  selection, `patchy.io`, `patchy.ui`, `patchy.recovery`, `patchy.scripts`,
  `patchy.plugins`, `patchy.gmic`; `patchy.plugins` and `layer.applyPlugin` front the 8BF host,
  [plugins.md](plugins.md); `patchy.gmic` and `layer.applyGmic` the G'MIC engine, [gmic.md](gmic.md)).
- `src/ui/script_vector*.{hpp,cpp}`: shape, paint, path, mask, organization and selection
  bindings ([vector-automation.md](vector-automation.md)); `src/ui/script_brush.cpp` and
  `brush_automation.*`: `patchy.brushes` ([brush-automation.md](brush-automation.md)).
- `src/ui/script_canvas_window.{hpp,cpp}`: interactive script windows (games/demos).
- `src/ui/script_editor_dialog.{hpp,cpp}` + `src/ui/js_syntax_highlighter.{hpp,cpp}`: the
  Script Manager UI (folder tree, shadow-override saves, context menu, run status area).
  The C:\ button and the "Command Line Example..." entry open the copyable-command dialog
  (below). A single click loads a tree script; with unsaved edits, selection changes never
  load or prompt, and switching goes through activation (double-click/Enter), which asks
  to discard. New seeds a runnable starter template, left unmodified so an untouched
  template never guards or prompts. Syntax, gutter and hover-card colors use theme roles
  and refresh on scheme changes.
- `src/ui/markdown_viewer_dialog.{hpp,cpp}`: the read-only Markdown viewer
  (QTextBrowser via setSource; relative images resolve against the .md file; anchors are
  repainted in the accent blue, since the importer's default is unreadable on dark).
  `MainWindow::open_scripting_guide()` owns the single instance shared by Help > Scripting
  Guide (`help.scripting_guide`) and the Script Manager's Help button.
- `src/ui/script_folders.{hpp,cpp}`: the script browser model (recursive bundled/user
  scans, the shadow-override merge), shared by the Scripts menu and the editor tree.
- `src/ui/main_window_scripting.cpp`: the File > Scripts menu, the script commands, and
  the CLI flows (`run_script_command`, `run_cli_script`).
- `scripts/bundled/` (repo): bundled scripts + `patchy.d.ts` + `scripting-guide.md`,
  staged next to the binaries by the `patchy_bundled_scripts` copy-once target (macOS:
  `Contents/Resources/scripts`, Linux install: `share/patchy/scripts`). ONLY
  `scripts/bundled` ships; the rest of `scripts/` is dev tooling and must never be staged
  (the copy step cleans the staged folder first, so renames/removals propagate; the macOS
  bundle POST_BUILD does the same into Resources/fonts, /translations and /scripts).
  Bundled scripts live in `Games/`, `Demos/`, `Effects/`, `Utilities/` (display names go
  through `script_folder_display_name` for localization). Convention: only `Games/`
  scripts create their own document or window; every other bundled script works on the
  ACTIVE document and alerts "Open a document first." when none is open, except that a
  Utilities script may create a document that IS its stated output (`contact-sheet.js`).
  Every bundled script must finish cleanly unattended under `--run-script` (cancelled
  pickers return "", showDialog answers its defaults). User scripts live in the per-user
  app-data folder under `scripts/` (`MainWindow::user_scripts_directory()`).
- Tests: `tests/ui/scripting_tests.cpp`.

## Script icons and metadata

Header directives live in the `//` comment block at the top of a script, read by
`read_script_metadata` (script_folders.cpp; parsing stops at the first non-comment line,
30 lines max):

- `// @name Breakout`: display name in the Script Manager tree and the File > Scripts
  menu (falls back to the file base name). Files sort by display name.
- `// @description ...`: the hover-card blurb; repeated lines join with a space.
- `// @author ...`: the hover-card credit line.
- `// @window`: the script creates its own window or document (a window badge); scripts
  without it work on the active document. Set on the three Games plus contact-sheet.js.
- `// @cli ...`: the argument part of the command-line example, everything after
  `--run-script <script>`, verbatim (repeated lines join like `@description`).
  `script_cli_example_command` (script_folders.cpp) builds exe path + `--run-script` +
  quoted script path + the `@cli` tokens. Without `@cli` it appends an ` example.png`
  placeholder for active-document scripts and nothing for `@window` scripts, so every
  script gets a working example; Utilities scripts that take `--script-arg` carry `@cli`.
- `// @hotkey Ctrl+Alt+D`: the script's DEFAULT shortcut (one PortableText sequence; a
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
dialog. Every interactive helper honors it, as do the open-time prompts (Missing Fonts,
Missing Linked Files, the CMYK proof notice, import notices, the raw develop dialog, the
open-failed box) and every dialog run through `exec_dialog`; pinned by
`ui_unattended_open_skips_the_missing_fonts_dialog` and `ui_unattended_open_skips_the_missing_links_dialog`.

The form dialog (shared by showDialog/showOptions, `run_form_dialog` in
script_engine.cpp) also supports `folder` and `file` field types (path line edit +
Browse; values travel as "/" paths) and the `description` header. Games deliberately
show no dialog (OPTIONS block only: a game should just start); trim-to-content and
save-version stay instant too.

## Shadow overrides (saving over a bundled script)

Save on a bundled script never touches the shipped file (read-only installs; app updates
would clobber edits): the editor writes a user copy at the SAME relative path under the
user scripts folder. The scan merge (`scan_scripts`) shows that copy in place of the
bundled entry, tagged "(modified)", and it is what runs from the menu, the editor, and
`include()`. "My Scripts" lists only non-overriding user scripts; Revert to Bundled
deletes the copy. Keep this override-by-relative-path rule intact everywhere a bundled
script is resolved.

## Engine rules

The binding design decisions of the engine (bindings, threading, timeouts, errors, memory) are in [scripting-engine.md](scripting-engine.md).

## Script hotkeys

Every script in the merged scan owns one persistent `QAction` (parent MainWindow), both its
File > Scripts entry and a HotkeyRegistry command of category `scripts`, so the menu shows
the shortcut and the key works from the first keypress.
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
- **Editor**: `build_rows` lists category `scripts` under "Scripts" by display name (the
  menu is built lazily, so the menu walk cannot be relied on). "Assign Hotkey..." calls
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
- Pinned by `ui_script_hotkey_runs_user_script`, `ui_script_hotkey_ids_follow_relative_path`,
  `ui_scripts_menu_lists_bundled_scripts`, `ui_hotkey_editor_lists_scripts_category`.

## Explicit save options

`doc.saveAs(path, options)` / `doc.exportAs(path, options)` (`ScriptDocumentObject::save_to_path`)
start from `MainWindow::image_save_defaults_for_document()`, overlay the object through
`apply_script_image_save_options` (src/ui/script_save_options.cpp), and call
`save_document_to_path` with `SaveToPathPolicy{flatten_confirmed, scripted, export_copy}`.
The parser is strict (an unknown, mistyped, out-of-range or other-extension key
throws before anything is written) and reuses the persisted tokens from
`image_save_option_keys.hpp`, shared with the dialogs and settings. No options object =
the old path (defaults or the session's remembered options). `scripted` skips the
saved-channels, Aseprite fill-opacity and blend-mode prompts and never persists the user's defaults or save directory; `export_copy` (exportAs) takes the save-a-copy branch for every format, so
the session's path, title and modified state stay untouched. Pinned by
`ui_script_save_as_options_write_dds_and_jpeg` and `ui_script_save_as_options_are_strict`.

## CLI and AI control

`patchy-mcp` (a persistent stdio MCP workspace: offscreen, `--visible`, or `--attach` to
the user's window) shares startup and this scripting engine; its setup, lifecycle and
protocol are in [ai-control.md](ai-control.md). The command line:

```
patchy [--headless] --run-script <file.js> [--script-output <out.txt>] [--script-arg key=value ...] [files...]
```

`--script-arg key=value` (repeatable) surfaces as `patchy.args.key` in the script (all
string values); the forwarded single-instance payload carries the raw tokens as extra
newline-separated fields after the output path, so keys and values must not contain
newlines. The bundled `Utilities/batch-export.js` is the reference consumer.

- With a running instance: the request forwards over the single-instance socket (the
  `patchy-cmd:run-script` reserved entry, same scheme as `--screenshot`) and the invoker
  exits immediately; file/script requests, Finder opens included, wait until any current
  script finishes. Console output, errors, and a final `[done]` or `[failed]` line reach
  the output file when the run fully completes; the caller polls for it. Warnings are
  prefixed `[warn] `, errors `[error] `; plain log lines are unprefixed so scripts can emit
  clean data (JSON included). Forwarded runs are unattended (above).
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
- Unattended runs (forwarded ones included): RAW imports read `.rawprefs` sidecars or
  defaults without writing settings, PDF imports use defaults, dialogs return Cancel,
  modified documents stay open unless the script calls `doc.close()`, forms normalize
  without showing, and menu Undo/Redo/Quit are rejected while the script owns the
  transaction.

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
- Manual smoke: every bundled script runs from File > Scripts and unattended under
  `--run-script` (picker-driven ones take `--script-arg` and cancel cleanly without).

## Future work

Not built: document/save/command hooks with a reentrancy design, persistent script
storage, macro recording, non-blocking batches, script packaging, an editor REPL.
Anti-goals: never freeze or fork the API surface, no undocumented escape hatches, and
scripts stay plain user-editable files.
