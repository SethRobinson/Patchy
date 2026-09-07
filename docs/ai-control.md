# Local AI control

Native vector automation uses `execute_script` with additive API 1 methods.
`get_info` advertises vector capabilities; `get_state` includes compact shape,
mask, path/revision, and target discovery, covered by attached state tokens.
`get_help` topics `vector-art`, `edit-shape`, and `paths-masks` serve examples from
the connected installation. See [vector-automation.md](vector-automation.md).

Desktop packages include a native `patchy-mcp` stdio connector and an installable
`patchy-control` skill. Agents can create documents, paint, inspect images, revise
layers in later requests, and save editable PSDs. No Python or Node runtime is
required. The JavaScript API edits through Patchy's existing engines.

## Setup and distribution

The user-facing entry point is Help > Set up AI Control (`help.ai_setup`,
`MainWindow::open_ai_setup_dialog()` in `main_window_scripting.cpp`, dialog class
`AiSetupDialog` in `src/ui/ai_setup_dialog.*`). It shows an English text the user
pastes into their AI assistant; the assistant reads the shipped setup document and
registers the connector and installs the skill itself. The setup text is built by `ai_setup_blurb_text` in
`src/ui/ai_control_paths.*`, which also owns the install-layout resolver
(`resolve_ai_control_paths`, `ai_control_skill_directory`) shared with
`patchy-mcp`'s `get_info`, so the connector and the dialog cannot disagree about
paths. Missing pieces read `NOT FOUND` in the text and as a warning in the dialog's
status label; Flatpak (`FLATPAK_ID` set) switches to the `flatpak run` command form
and the in-sandbox skill path. The blurb is deliberately not translated: its reader
is the assistant. The action is hidden on wasm (no connector) but its command id
stays registered. Dialog objectNames for tests: `aiSetupDialog`, `aiSetupBlurbText`,
`aiSetupStatusLabel`, `aiSetupCopyButton`, `aiSetupOpenSkillFolderButton`,
`aiSetupOpenGuideButton`, `aiSetupCloseButton`, `aiSetupExamplesLabel`,
`aiSetupExamplesComboBox`, `aiSetupExampleText`, and `aiSetupExampleCopyButton`.
One fixed setup prompt creates a default connection with no mode argument, or
reuses a matching existing connection with its current arguments. A separate,
localized example selector changes only the task prompt below it. Setup and
examples have independent Copy buttons; selecting an example never changes the
setup text or client configuration. Examples cover open-document fixes and
review, visible art, background icons and contact sheets, reference art, and
export sizes. The dialog and both prompt fields use larger proportional fonts;
local stylesheet font rules override MainWindow's inherited text size.
The served workflow routes tasks through an appropriate existing MCP connection
or the CLI without reinstalling. MCP-only clients still need a reconnection for
an explicitly requested startup-mode change; there is no runtime mode-switch tool.
The instructions install only the `SKILL.md` entry point, require saved test files, and distinguish configuration from
successful tool verification, including clients that need a restart.

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

`agent-kit/patchy-control/SKILL.md` is the stable client entry point. Install only
that file in the client's `patchy-control` skill folder. The full workflow lives
in `references/workflow.md`; `get_help(workflow)` returns it, including when
`topic` is omitted. API references, setup, and examples remain with Patchy and
are fetched through `get_help` or read from that installation for CLI use.
Existing topic identifiers remain unchanged. The connector reads the files for
each request, so workflow updates do not require recopying a client skill.
After upgrading the executable, reconnect before editing so code and help match.
Entry-point changes require an explicit one-file refresh; migrate old full-folder
installs by removing only verified unmodified package files, preserving local edits.

`agent-kit/patchy-control` owns the entry point, workflow, setup, and examples. The shared
`patchy_agent_kit` CMake target assembles `build/<preset>/ai/patchy-control`, copying
the authoritative `scripts/bundled/patchy.d.ts` and `scripting-guide.md` into its
references. This assembled folder stays with Patchy. API docs are not duplicated in source.
Windows stages the connector, `ai`, and `scripts` beside the application. macOS
copies the connector into `Contents/MacOS` and the kit into `Contents/Resources/ai`;
`macdeployqt` processes both executables. Linux installs the connector in `bin`
and the kit in `share/patchy/ai`, including Flatpak's `/app` prefix. Package scripts
run `patchy-mcp --check` against the staged layout. This validates startup, native
painting, preview rendering, and installed help resources without a client.

## Workspace and protocol

`src/app/main.cpp` shares Qt, fonts, localization, and theme initialization between
the application and console connector. With no arguments, the connector creates
an offscreen MainWindow with temporary settings and bypasses single-instance
forwarding, sound, and updates. `--visible` shows that separate workspace. Its
documents and history disappear on disconnect. Closing stdin interrupts work and
waits for workers before destroying that owned workspace.

The startup argument `--visible` selects the desktop Qt backend and shows
the connector's own workspace so the user can watch batches appear. An explicit
`QT_QPA_PLATFORM` is respected in that mode; `get_info` reports actual `mode`,
`platform`, and `windowVisible`, not just the requested mode. Hidden and visible
sessions share settings isolation, scripting restrictions, and stdin lifetime.
Brush tips and complete brush presets use the artist's persistent library in both
workspaces. The connector captures its original settings filename before redirecting
window preferences; `PATCHY_SETTINGS_DIR` still redirects both for owned tests.
Visible connector windows allow normal user dialogs between requests, including
Save/Discard/Cancel when closing a modified document or the window. Only the MCP
script run suppresses those prompts. Hidden workspaces suppress prompts for their
entire lifetime. `configure_owned_mcp_workspace` owns this startup policy.
Closing the visible window after resolving save prompts exits its connector;
Cancel keeps the window and connection open. The client can start a new workspace
on reconnect. Closing an unchanged document requires no prompt in either mode.
Mode changes require saving, reconnecting, and reopening with new IDs. Avoid
manual editing during agent operations. Visible mode needs a desktop display;
`--check`, help, and malformed invocations remain offscreen.

`--attach` is a stdio proxy to the already-running interactive Patchy from the
same installation. It never creates a replacement window. `get_info` reports
`workspace` (`attached` or `isolated`), `liveWindowAttachment`,
`requiresExpectedState`, and the document-owning `processId` in addition to actual
display metadata. Closing the proxy, its stdin, or its client connection stops
only its own request; the artist's window, unsaved documents, and history remain.
Closing Patchy closes the proxy even if its client's stdin remains open.

`ui/mcp_attachment.*` owns a per-user local socket with `UserAccessOption`, scoped
by installation directory and home directory. `PATCHY_MCP_ENDPOINT` selects an
explicit endpoint for multiple instances and isolated automation. A QThread owns
all socket reads/writes and processes cancellation independently of UI work. Only
one attached client is accepted; additional connections are closed, never queued.
The listener starts only for the interactive desktop app, not headless, script,
export, stress, screenshot, or wasm runs. It is destroyed before MainWindow and
its scripting host. No TCP/HTTP listener or hosted-chat connection is provided.

`src/app/mcp_server.cpp` owns connector startup and proxying;
`app/mcp_stdio.*` owns interruptible binary stdio; `ui/mcp_session.*` owns shared
JSON-RPC dispatch for attached and isolated workspaces. Stdout
is exclusively protocol; Qt diagnostics use stderr. Supported revisions are
2025-11-25 and 2025-06-18. Initialization returns tools capability and server
instructions. Messages are bounded to 16 MiB. Tool errors use `isError` and
structured details; malformed requests use JSON-RPC errors. Image results
contain PNG MCP image content plus text and `structuredContent` metadata.

| Tool | Purpose |
|---|---|
| `get_info` | Versions, capabilities, actual display mode/visibility, skill directory, trust model |
| `get_help` | Workflow, API, guide, reference-art workflow, or one of three examples |
| `get_state` | Documents, layer hierarchy, IDs, dimensions, selection, modified state, history, state token |
| `execute_script` | Fresh JavaScript globals over persistent documents; JSON result and separate logs |
| `draw_strokes` | Native Brush/Eraser batch targeting document/layer IDs |
| `get_preview` | Fresh canvas PNG with crop/scale metadata and state token, or the connected window capture |
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

## Attached editing and activity

Each mutating tool (`execute_script`, `draw_strokes`, `undo`, `redo`) accepts
`expectedState`. Attached sessions require it to equal the latest `stateToken`
returned by `get_state`, a preview, or a mutation's returned state. It is optional
for isolated sessions. A mismatch or missing token returns `stale_state` with
current state before any edit. The assistant must inspect the new preview before
retrying. Tokens combine a connection nonce with a fingerprint of IDs, active
document/layer, session/history revisions, layer render revisions, selection
geometry, palette revision, channel/path revisions, and canvas mask state. No
layer pixel scan is used. Reconnect changes the nonce even if documents survive.

Document tools refuse an unfinished pointer gesture, text edit, transform, crop,
preview dialog, modal dialog, or other script with `busy`. Connector restrictions
are scoped to the owned script run, so an idle connection does not restrict or
interrupt local user scripts. CLI/Finder file opens wait until a script ends.

`ui/mcp_activity.*` installs a permanent status-bar widget (`mcpActivity`,
`mcpActivityLabel`, `mcpStopButton`). It appears after initialization, says AI
connected while idle, and distinguishes reading from editing during requests.
Its tooltip identifies the client and explains that idle can mean model thinking.
It hides on disconnect. During work, scrolling panels, browsing menus, Preferences,
About, window movement and view navigation remain available. Conflicting edits
explain that Pause is required. Pause/Resume (`mcpPauseButton`) shares
`patchy.ui.paused`. Pausing waits for the current native edit; Resume appears when
manual editing is safe. Manual edits keep their own Undo steps. Resume resolves
targets again and reports deleted or incompatible targets without crashing.
The request stays busy throughout; Pause resets on completion or cancellation.

Stop remains visible but disabled between edit requests, so its location is
discoverable. Visible unattended CLI scripts use their own status-bar activity
and Stop control. Progressive painting, explicit `patchy.ui.present` checkpoints,
and resize progress are described in [automation-feedback.md](automation-feedback.md).

An owned connector run supplies a throttled progress callback to ScriptEngineHost.
API calls pump UI events under the input guard, including native stroke samples,
so previews can update and Stop can cancel without a modal dialog. The existing
engine/timer reentrancy gates remain required. Stop retains changes and available
undo history. Pure JavaScript without API calls cannot pump the UI; protocol
cancellation and the inactivity watchdog still interrupt it from another thread.
Connected means a client is attached, not that the AI is computing or has finished.

## Scripting contracts

The additive API remains version 1. Read the packaged TypeScript reference and
[scripting guide](../scripts/bundled/scripting-guide.md) for signatures and examples.

- Document `id` and layer `id` are decimal strings, avoiding JavaScript number
  precision loss. A document ID is valid until close in its owning Patchy process;
  layer IDs are scoped to the document and valid while that layer exists. History
  can remove/restore a layer. Re-query state after history changes and reopen.
  `app.getDocument(id)` and `doc.getLayer(id)` report stale/invalid IDs.
- `doc.modified`, `canUndo`, and `canRedo` report state. By default each run creates
  one snapshot per affected document. `slowMode` in state reports the workspace's
  Slow toggle, which separates native strokes and undoable edits within history
  limits. `doc.undo()` and `redo()` must precede new
  mutations in the same script. Errors and cancellation retain available undo
  history and can leave partial changes, reported with state and logs.
  Connector requests reject `app.undoEnabled = false`.
- `patchy.setResult(value)` returns JSON independently of console output. The
  serialized result is bounded to 4 Mi characters. Script source has the same
  bound. Logs are capped at 1000 entries of 16000 characters each.
- `layer.drawStrokes` parses the entire batch before painting. It temporarily
  selects the target and native Brush/Eraser/Mixer settings, then restores them. It
  shares the native stroke lifecycle, spacing, midpoint smoothing, Flow/opacity
  accumulation, selection clipping, palette snapping, and deterministic seed.
  Pressure defaults to the documented pen mapping. Tips, full dynamics, Mixer,
  pen pose, smoothing and timed airbrush are exposed; `patchy.brushes` discovers,
  previews and saves brushes. See [brush-automation.md](brush-automation.md).
  Exact pixel art uses `setPixels` and `fillRect`; public types own the limits.
- `doc.renderPreview(path, options)` writes PNG atomically through QSaveFile.
  MCP previews encode the same CPU composite directly into image content. Neither
  preview route changes the save path or modified state. Full canvas or clipped
  rectangles preserve aspect ratio with bounded output; nearest-neighbor scaling
  can enlarge sprites. Coordinates map as
  `documentX = rect.x + previewX / scaleX`, similarly for Y. Canvas previews are
  fresh renders; app-window captures report their actual offscreen status. Window
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

The served workflow teaches discovery, batched edits, image inspection, checkpoints, undo,
and PSD plus PNG delivery. Its examples create layered pixel art, pressure paint,
and an accent layer in an existing file. The alternative entry point is
`patchy --headless --run-script file.js --script-arg key=value`; see
[scripting.md](scripting.md) for output capture and process lifetime.

`tests/mcp_client_tests.py` drives the installed connector with the official Python
MCP client SDK as a development-only dependency. It also checks raw JSON-RPC busy,
cancellation, recovery, malformed messages, and disconnect behavior.
`ui_mcp_*` tests attached unsaved state/history, stale pixel edits and tab changes,
reconnect tokens, input locking, Stop, local-script independence, and tight-loop
cancellation. The SDK suite also launches an isolated offscreen interactive app
and tests the real attached proxy, reconnect lifetime, and app-close behavior.
`ui_script_automation_*` checks stroke parity with native brush output, seeded
pressure/selection/erasing/palette behavior, Unicode previews, unchanged save
state, stale IDs, and interactive-operation errors. See [testing.md](testing.md)
for commands and release handoff requirements.
