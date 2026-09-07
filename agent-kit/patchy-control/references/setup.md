# Setting up AI control of Patchy

Patchy ships two pieces for AI assistants: `patchy-mcp`, a local MCP connector
that lets an assistant create, paint, inspect, and save layered artwork, and
`patchy-control`, a skill folder that teaches the assistant how to use it well.
The connector runs on this computer, hidden by default or in a separate visible
window when requested. It never attaches to the artist's existing window. The
connector itself has no network connection; previews and tool results are returned
to the AI client and may be processed by that client's hosted model.

## For people

1. Open Patchy and choose Help > Set up AI Control.
2. Choose whether to show the AI's work in a separate window, then click Copy to
   Clipboard. Leave the checkbox clear for hidden work with previews in chat.
3. Paste the text into your AI assistant (Claude Code, Codex, Cursor, or another
   tool that supports MCP) and send it.

The assistant reads this document, registers the connector, installs the skill,
and shows you a small test drawing when it is done. Some clients need you to
reconnect or restart before tools become available in the current chat.
The rest of this page is written for the assistant, and for anyone who prefers
to do the steps by hand.

## For the AI agent doing the setup

Configure only the requested Patchy connector and skill:

1. Register the connector as a stdio MCP server named `patchy`. Add it beside
   any existing servers. Do not change, remove, or reorder other servers or
   settings. Inspect the existing `patchy` entry first. Reuse a matching entry;
   if it points to a different installation, report the conflict before replacing
   it. Preserve other configuration content, including its order.
2. Install the skill by copying the whole `patchy-control` folder, keeping its
   name, into your skills directory. Skip this if your tool has no skills
   feature; the connector works without it. Reuse an identical installed copy;
   preserve local skill customizations if updating an existing Patchy skill.
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

The connector needs no Python or Node runtime. With no arguments it uses an
offscreen workspace. Add the single argument `--visible` when the user asks to
watch it work in a separate Patchy window. For Flatpak append `--visible` after
the app ID. Keep the executable and arguments separate in the client config.
Both modes use the same tools and isolate the artist's existing windows and
preferences. A visible session needs a working desktop display.

The client starts and stops its workspace. Switching modes requires saving the
PSD, changing only Patchy's arguments, reconnecting, and reopening the saved PSD;
restarting loses unsaved documents and undo history. Do not restart the AI client
yourself if that would interrupt the conversation. Tell the user what is ready
and the exact remaining restart step.

For Flatpak, copy the skill out of the sandbox with
`flatpak run --command=cp com.rtsoft.patchy -R /app/share/patchy/ai/patchy-control <destination>`
where the destination is a folder the sandbox can see. The connector can also
read the skill for you through its `get_help` tool.

From a source checkout, build a desktop preset first. CMake assembles the skill
into `build/<preset>/ai/patchy-control` with the current API reference and
guide; never install the unassembled `agent-kit` source folder.

## Per-client steps

Replace `<connector>` with the connector path from the table and `<skill>` with
the skill folder.

**Claude Code**

```powershell
claude mcp add patchy -- "<connector>"
```

Copy `<skill>` to `~/.claude/skills/patchy-control` (or a project's
`.claude/skills/patchy-control`). Run `/mcp` or restart if the server does not
appear.

**Codex**

```powershell
codex mcp add patchy -- "<connector>"
```

Copy `<skill>` to `~/.agents/skills/patchy-control` (or a project's
`.agents/skills/patchy-control`). For visible work append `--visible` to the
connector command: `codex mcp add patchy -- "<connector>" --visible`.

If editing `config.toml` directly, add only `[mcp_servers.patchy]`. On Windows,
a TOML literal string such as `command = 'C:\Users\Name\...\patchy-mcp.exe'`
preserves backslashes. For visible work use `args = ["--visible"]`.

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
For a visible native workspace use `"args": ["--visible"]`; for Flatpak append
it to that argument list.

Install the skill wherever the client documents its skills folder, if it has
one.

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
2. Call `get_help` with `topic: "api"` and `get_state`.
3. Create a 64x64 document, draw a small smiley face, and inspect the `get_preview`
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

## Things to ask Patchy to do

- "Turn this photo into a 64x64 pixel portrait. Show drafts, preserve the cap and
  expression, and deliver an editable PSD plus a PNG."
- "Work visibly so I can watch. Make three layered icon variations, show me the
  previews, then refine my favorite."
- "Work hidden. Open these sprites, crop transparent margins, and export copies
  to a new folder. Keep the originals."
- "Open this PSD, add a highlight layer, and compare before and after. Save a
  separate edited copy."

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
