# Local AI control

Desktop packages include a native `patchy-mcp` stdio connector and an installable
`patchy-control` skill. Agents can create documents, paint, inspect images, revise
layers in later requests, and save editable PSDs. No Python or Node runtime is
required. The JavaScript API edits through Patchy's existing engines.

## Setup and distribution

The user-facing entry point is Help > Set up AI Control (`help.ai_setup`,
`MainWindow::open_ai_setup_dialog()` in `main_window_scripting.cpp`, dialog class
`AiSetupDialog` in `src/ui/ai_setup_dialog.*`). It shows an English text the user
pastes into their AI assistant; the assistant reads the shipped setup document and
registers the connector and installs the skill itself. The dialog never shows
commands or JSON to the person. The text is built by `ai_setup_blurb_text` in
`src/ui/ai_control_paths.*`, which also owns the install-layout resolver
(`resolve_ai_control_paths`, `ai_control_skill_directory`) shared with
`patchy-mcp`'s `get_info`, so the connector and the dialog cannot disagree about
paths. Missing pieces read `NOT FOUND` in the text and as a warning in the dialog's
status label; Flatpak (`FLATPAK_ID` set) switches to the `flatpak run` command form
and the in-sandbox skill path. The blurb is deliberately not translated: its reader
is the assistant. The action is hidden on wasm (no connector) but its command id
stays registered. Dialog objectNames for tests: `aiSetupDialog`, `aiSetupBlurbText`,
`aiSetupStatusLabel`, `aiSetupCopyButton`, `aiSetupOpenSkillFolderButton`,
`aiSetupOpenGuideButton`, `aiSetupCloseButton`.

The [packaged setup document](../agent-kit/patchy-control/references/setup.md) is
what the assistant reads (locally from the skill's `references` folder, or the
GitHub copy the blurb also names). It carries the per-platform paths, per-client
steps, and verification. The Windows installer installs to
`%LOCALAPPDATA%\Programs\Patchy`, never Program Files; keep every example on
that path. Register the connector and install the skill separately. Putting a
skill in a repository does not install it in a client.

For a Windows source build, the same dialog works from `build/release/patchy.exe`:
it resolves `build/release/patchy-mcp.exe` and the assembled
`build/release/ai/patchy-control`. Use absolute output paths or configure the
server's working directory.

`agent-kit/patchy-control` owns the skill, setup, and three examples. The shared
`patchy_agent_kit` CMake target assembles `build/<preset>/ai/patchy-control`, copying
the authoritative `scripts/bundled/patchy.d.ts` and `scripting-guide.md` into its
references. Install that assembled folder. API docs are not duplicated in source.
Windows stages the connector, `ai`, and `scripts` beside the application. macOS
copies the connector into `Contents/MacOS` and the kit into `Contents/Resources/ai`;
`macdeployqt` processes both executables. Linux installs the connector in `bin`
and the kit in `share/patchy/ai`, including Flatpak's `/app` prefix. Package scripts
run `patchy-mcp --check` against the staged layout. This validates startup, native
painting, preview rendering, and installed help resources without a client.

## Workspace and protocol

`src/app/main.cpp` shares Qt, fonts, localization, and theme initialization between
the application and console connector. The connector forces offscreen operation,
uses a temporary settings directory, and bypasses single-instance forwarding,
sound, and update checks. Each process owns one MainWindow workspace and its
documents/history. It never attaches to another Patchy process. No HTTP listener
or hosted-chat connection is provided. Closing stdin interrupts active work,
stops timers, exits the event loop, and waits for owned workers before destroying
the workspace. Unsaved documents do not survive disconnect or restart.

`src/app/mcp_server.cpp` owns newline-delimited JSON-RPC over binary stdio. Stdout
is exclusively protocol; Qt diagnostics use stderr. Supported revisions are
2025-11-25 and 2025-06-18. Initialization returns tools capability and server
instructions. Messages are bounded to 16 MiB. Tool errors use `isError` and
structured details; malformed requests use JSON-RPC errors. Image results
contain PNG MCP image content plus text and `structuredContent` metadata.

| Tool | Purpose |
|---|---|
| `get_info` | Versions, capabilities, skill directory, trust model |
| `get_help` | Workflow, API, guide, or one of three examples |
| `get_state` | Documents, layer hierarchy, IDs, dimensions, selection, modified state, history |
| `execute_script` | Fresh JavaScript globals over persistent documents; JSON result and separate logs |
| `draw_strokes` | Native Brush/Eraser batch targeting document/layer IDs |
| `get_preview` | Fresh canvas PNG with crop/scale metadata, or offscreen app-window capture |
| `undo`, `redo` | Restore one document history step |

Document operations execute on the Qt UI thread. A dedicated input thread keeps
cancellation and busy responses responsive while JavaScript is running. Only one
tool call may be outstanding; concurrent calls receive `busy` and are not queued
or retried. Cancellation IDs must match the current request. The input thread
uses the host's mutex-protected QJSEngine interrupt gate; native stroke batches
check interruption between path samples. Other native operations finish their
current operation before cancellation is observed. The existing inactivity
watchdog remains active. Scripts finish after their timers finish; an infinite
interval requires cancellation.

Engine creation and destruction coordinate with the input thread, including
cancellation before evaluation. A completed request releases its busy slot before
sending its reply. Disconnect does not silently save, retry mutations, or leave
a background child process running.

## Scripting contracts

The additive API remains version 1. Read the packaged TypeScript reference and
[scripting guide](../scripts/bundled/scripting-guide.md) for signatures and examples.

- Document `id` and layer `id` are decimal strings, avoiding JavaScript number
  precision loss. A document ID is valid until close in that connector process;
  layer IDs are scoped to the document and valid while that layer exists. History
  can remove/restore a layer. Re-query state after history changes and reopen.
  `app.getDocument(id)` and `doc.getLayer(id)` report stale/invalid IDs.
- `doc.modified`, `canUndo`, and `canRedo` report state. Each mutating run creates
  one snapshot per affected document. `doc.undo()` and `redo()` must precede new
  mutations in the same script. Errors and cancellation retain available undo
  history and can leave partial changes, reported with state and logs.
  Connector sessions reject `app.undoEnabled = false`.
- `patchy.setResult(value)` returns JSON independently of console output. The
  serialized result is bounded to 4 Mi characters. Script source has the same
  bound. Logs are capped at 1000 entries of 16000 characters each.
- `layer.drawStrokes` parses the entire batch before painting. It temporarily
  selects the target and round Brush/Eraser settings, then restores them. It
  shares the native stroke lifecycle, spacing, midpoint smoothing, Flow/opacity
  accumulation, selection clipping, palette snapping, and deterministic seed.
  Pressure uses default pen mapping. Bitmap tips, tilt, stabilizers, and timed
  airbrush samples are outside this API. Exact pixel art uses `setPixels` and
  `fillRect`. Limits and defaults belong in `PatchyStroke`.
- `doc.renderPreview(path, options)` writes PNG atomically through QSaveFile.
  MCP previews encode the same CPU composite directly into image content. Neither
  preview route changes the save path or modified state. Full canvas or clipped
  rectangles preserve aspect ratio with bounded output; nearest-neighbor scaling
  can enlarge sprites. Coordinates map as
  `documentX = rect.x + previewX / scaleX`, similarly for Y. Canvas previews are
  fresh renders; app-window captures are explicitly labeled offscreen. Window
  captures show the view as staged by `patchy.ui.zoom` (percent) and
  `patchy.ui.fitOnScreen()`, which connector sessions allow; canvas previews
  ignore the view.
- The trusted-script model is unchanged. Scripts can access files with application
  privileges. Connector sessions reject `app.runCommand` and interactive script
  canvases; use explicit document APIs. Existing unattended option dialogs return
  their defaults/argument overrides. No permission dialog appears on the desktop.

`src/ui/script_automation.cpp` owns state, lookup, preview, history, and batch
validation. `src/ui/canvas_widget_script_stroke.cpp` owns the direct native stroke
lifecycle entry point; it does not synthesize Qt or desktop input events.

## Workflow and validation

The skill teaches discovery, batched edits, image inspection, checkpoints, undo,
and PSD plus PNG delivery. Its examples create layered pixel art, pressure paint,
and an accent layer in an existing file. The alternative entry point is
`patchy --headless --run-script file.js --script-arg key=value`; see
[scripting.md](scripting.md) for output capture and process lifetime.

`tests/mcp_client_tests.py` drives the installed connector with the official Python
MCP client SDK as a development-only dependency. It also checks raw JSON-RPC busy,
cancellation, recovery, malformed messages, and disconnect behavior.
`ui_script_automation_*` checks stroke parity with native brush output, seeded
pressure/selection/erasing/palette behavior, Unicode previews, unchanged save
state, stale IDs, and interactive-operation errors. See [testing.md](testing.md)
for commands and release handoff requirements.
