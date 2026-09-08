# Setting up AI control of Patchy

Patchy ships two pieces for AI assistants: `patchy-mcp`, a local MCP connector
that lets an assistant create, paint, inspect, and save layered artwork, and
`patchy-control`, a skill folder that teaches the assistant how to use it well.
The connector runs on this computer. It can connect to the artist's open Patchy
workspace, work in a separate visible window, or work hidden. The
connector itself has no network connection; previews and tool results are returned
to the AI client and may be processed by that client's hosted model.

## For people

1. Open Patchy and choose Help > Set up AI Control.
2. Click Copy Setup Prompt. This is a one-time installation step.
3. Paste the text into your AI assistant (Claude Code, Codex, Cursor, or another
   tool that supports MCP) and send it.

The assistant reads this document, registers the connector, installs the skill,
and shows you a small test drawing when it is done. Some clients need you to
reconnect or restart before tools become available in the current chat.
After setup, choose an example in the same dialog and click Copy Example Prompt.
Examples include editing your open document, working visibly, and background jobs.
Changing the example leaves the setup prompt and installed settings alone.
The rest of this page is written for the assistant, and for anyone who prefers
to do the steps by hand.

## For the AI agent doing the setup

Configure only the requested Patchy connector and skill:

1. Register the connector as a stdio MCP server named `patchy`. Add it beside
   any existing servers. Do not change, remove, or reorder other servers or
   settings. Inspect the existing `patchy` entry first. Reuse a matching entry;
   if it points to a different installation, report the conflict before replacing
   it. Preserve other configuration content, including its order.
2. Create `patchy-control` in your skills directory and copy only the package's
   `SKILL.md` into it. Leave `references` and `scripts` with Patchy. Skip this if
   your tool has no skills feature; the connector works without it. Reuse an
   identical installed file and preserve local customizations when updating.
3. Verify (see below). If a path you were given says NOT FOUND, report what is
   missing and stop instead of guessing.

The pasted text from Help > Set up AI Control already contains the absolute
paths for this installation. If you only have this document, find them in the
table below.

## Where the pieces are

| Installation | Connector command | Skill folder |
|---|---|---|
| Windows installer | `%LOCALAPPDATA%\Programs\Patchy\patchy-mcp.exe` | `%LOCALAPPDATA%\Programs\Patchy\ai\patchy-control` |
| Windows zip | `<unpacked folder>\patchy-mcp.exe` | `<unpacked folder>\ai\patchy-control` |
| macOS | `/Applications/Patchy.app/Contents/MacOS/patchy-mcp` | `/Applications/Patchy.app/Contents/Resources/ai/patchy-control` |
| Linux prefix install | `<prefix>/bin/patchy-mcp` | `<prefix>/share/patchy/ai/patchy-control` |
| Linux Flatpak | program `flatpak`, arguments `run --command=patchy-mcp com.rtsoft.patchy` | `/app/share/patchy/ai/patchy-control` inside the sandbox |
| Source build | `build/<preset>/patchy-mcp` (`.exe` on Windows) | `build/<preset>/ai/patchy-control` |

`%LOCALAPPDATA%` is normally `C:\Users\<name>\AppData\Local`. In PowerShell the
installer path is `"$env:LOCALAPPDATA\Programs\Patchy\patchy-mcp.exe"`. Always
use the absolute path, quoted, because user folders often contain spaces.

The connector needs no Python or Node runtime. New installations use no arguments.
Reuse an existing matching entry, including its mode. Tasks can use that connector
or the same installation's CLI; see the workspace guidance in `get_help(workflow)`.
Do not reinstall or rewrite MCP settings when the user changes example prompts.

These startup modes remain available for clients that explicitly need one:

| Argument | Workspace |
|---|---|
| `--attach` | The user's Patchy from the same installation, including unsaved documents. Discovery remains available while Patchy is closed. Workspace requests report unavailability; a later read retries attachment. Never creates a substitute window. |
| `--visible` | A separate visible workspace owned by the connector. |
| No argument | A separate offscreen workspace owned by the connector. |

For Flatpak append the chosen argument after the app ID. Keep the executable and
arguments separate in client configuration. Never combine modes. The two separate
workspace modes isolate the artist's windows and preferences. Visible work needs
a desktop display. Only one attached MCP client can use a workspace at a time.

The client starts the connector when connecting. For isolated workspaces it also
controls document lifetime: disconnecting loses unsaved documents and history.
In attached mode, disconnecting leaves the artist's documents open and stops any
active AI request. Closing Patchy leaves the MCP proxy available: open Patchy and
call `get_info` again on the same connection. A `workspace_disconnected` error can
mean an interrupted edit made changes; inspect before repeating it. If a client
has no shell access and the user explicitly wants a
different MCP startup mode, save checkpoints, change only Patchy's arguments,
reconnect, and query the current IDs and state. This is a connection preference,
not another installation. Do not restart the AI client
yourself if that would interrupt the conversation. Tell the user what is ready
and the exact remaining restart step.

For Flatpak, create the destination `patchy-control` folder, then copy the entry
point with `flatpak run --command=cp com.rtsoft.patchy /app/share/patchy/ai/patchy-control/SKILL.md <destination>/SKILL.md`
where the destination is a folder the sandbox can see. The full workflow stays
inside the sandbox and is read through `get_help`.

From a source checkout, build a desktop preset first. CMake assembles the skill
into `build/<preset>/ai/patchy-control` with the current API reference and
guide. The assistant installs only `SKILL.md` from that assembled folder.

## Per-client steps

Replace `<connector>` with the connector path from the table and `<skill>` with
the skill folder.

**Claude Code**

```powershell
claude mcp add patchy -- "<connector>"
```

Create `~/.claude/skills/patchy-control` (or a project's
`.claude/skills/patchy-control`) and copy `<skill>/SKILL.md` into it. Run `/mcp` or restart if the server does not
appear.

**Codex**

```powershell
codex mcp add patchy -- "<connector>"
```

Create `~/.agents/skills/patchy-control` (or a project's
`.agents/skills/patchy-control`) and copy `<skill>/SKILL.md` into it. The default
connection needs no arguments. Preserve an existing matching connection.

If editing `config.toml` directly, add only `[mcp_servers.patchy]`. On Windows,
a TOML literal string such as `command = 'C:\Users\Name\...\patchy-mcp.exe'`
preserves backslashes. Leave arguments empty for a new default connection. The
optional `--attach` and `--visible` startup modes use the `args` array; they are
not choices the user needs to make during installation.

**Cursor and other JSON-configured clients**

Add this entry under `mcpServers` in the client's MCP configuration file. On
Windows, double every backslash inside the JSON string.

```json
{
  "mcpServers": {
    "patchy": {
      "command": "C:\\Users\\<name>\\AppData\\Local\\Programs\\Patchy\\patchy-mcp.exe",
      "args": []
    }
  }
}
```

For Flatpak use `"command": "flatpak"` and
`"args": ["run", "--command=patchy-mcp", "com.rtsoft.patchy"]`.
Optional startup modes go in `args`; for Flatpak they follow the app ID.

Install just `SKILL.md` in a `patchy-control` folder inside the client's skills
directory, if it has one.

**Claude Desktop**

Claude Desktop cannot edit its own configuration from a chat. A person adds the
same JSON entry to `claude_desktop_config.json` (Settings > Developer > Edit
Config) and restarts the app. Claude Desktop has no skills folder; the connector
still serves the skill through `get_help`.

If your client supports a working directory for stdio servers, set it to the
artwork output folder. Otherwise use absolute paths in scripts; Patchy does not
infer the agent's working directory from the conversation.

## Verify

Reconnect or restart the client if it does not list the new server. Then:

1. Call `get_info`. It reports Patchy's version, capabilities, actual display
   mode, window visibility, and the skill directory it found. If visible work
   was requested but `mode` is `offscreen`, check the display and the client's
   `QT_QPA_PLATFORM` environment before claiming the window is visible.
2. Call `get_help` with `topic: "workflow"`, then `topic: "api"`, then `get_state`.
   Verify `workspace` and `liveWindowAttachment` match the requested mode. Attached
   mutations require `expectedState` from the latest `stateToken`; follow the
   workflow's stale-state handling. The status bar shows AI connected while idle
   and AI reading or AI editing during requests, with Stop during an edit.
3. Create a new 64x64 document without changing existing documents, draw a small smiley face, and inspect the `get_preview`
   image with nearest-neighbor enlargement. Save a PSD and a native-size PNG to
   explicit output paths and return the image and those paths.

Report setup and verification separately. Configuration can be correct even if
this chat has not refreshed its tool catalog. A shell-capable agent can diagnose
startup with `"<connector>" --check` or exercise stdio with an MCP client, but
must describe that as a direct connector test, not proof that this chat has
loaded the tools. The application and connector need no Python or Node; a
development test client may use either.

A failed `get_info` can mean missing files, a startup/dependency error, client
permissions, a stale tool catalog, or a timeout. Inspect the actual error and
stderr instead of assuming the path is wrong. If the returned skill directory
is empty, the installation's assembled `ai/patchy-control` folder is missing.

## Updating Patchy and the entry point

The installed `SKILL.md` is a small entry point that fetches the connected
installation's current workflow and API. `get_help` reads the packaged files on
each request; the client does not need copied API references or examples.
After upgrading Patchy, save work and reconnect the MCP server so the running
executable matches the new package, then fetch the help again. If Patchy moved,
update only its connector command in the client configuration.

Ordinary API and workflow updates need no changes in the client skill folder.
If the entry point itself changes, explicitly replace that one file while
preserving customizations, and refresh the client's skills if necessary. Client
skill discovery does not synchronize files with the Patchy installation.

To migrate an older full-folder copy, compare its files with the corresponding
old package before removing them. Replace the unmodified `SKILL.md` and remove
only unmodified packaged references/examples. Preserve custom files and edits;
the entry point instructs the assistant to use current server help instead of
old copies. Never remove another skill or change unrelated MCP configuration.

## Things to ask Patchy to do

- "Look at the document I have open in Patchy and fix the face on a new layer.
  Show me before and after, then leave it open so I can keep editing."
- "Turn this photo into a 64x64 pixel portrait. Show drafts, preserve the cap and
  expression, and deliver an editable PSD plus a PNG."
- "Work visibly so I can watch. Make three layered icon variations, show me the
  previews, then refine my favorite."
- "Work hidden. Open these sprites, crop transparent margins, and export copies
  to a new folder. Keep the originals."
- "Open this PSD, add a highlight layer, and compare before and after. Save a
  separate edited copy."
- "Export every image in this folder as PNG at three sizes into new folders."
- "Make a labeled contact sheet of these images so I can compare them."
- "Create three color treatments of this product photo and show the results."
- "Use this template to create name badges from my list, with editable text."
- "Report each PSD's dimensions, layer names, visibility, and blend modes."
- "Apply the same crop, border, and watermark to a folder of screenshots."
- "Make a repeatable texture or geometric background from this color palette."

For reference-based art, read `get_help` with `topic: "reference-art"`. The
workflow uses deliberate drawing and visual iteration. Patchy does not infer
artwork from a photo by itself; the assistant chooses the shapes and edits.

## Protocol notes

Supported protocol versions: MCP 2025-11-25 and 2025-06-18, over stdio.
Messages use JSON-RPC IDs; stdout carries protocol messages and diagnostics go
to stderr. Requests are serialized. Cancellation stops JavaScript and its
timers; a native operation already in progress reaches its next interruption
boundary. The server exposes no HTTP listener and does not connect to hosted
chat services.

Online copy of this page:
https://github.com/SethRobinson/Patchy/blob/main/agent-kit/patchy-control/references/setup.md
