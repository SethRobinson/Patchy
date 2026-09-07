# Setting up AI control of Patchy

Patchy ships two pieces for AI assistants: `patchy-mcp`, a local MCP connector
that lets an assistant create, paint, inspect, and save layered artwork, and
`patchy-control`, a skill folder that teaches the assistant how to use it well.
Nothing is sent online; the connector runs a hidden background copy of Patchy on
this computer and never touches the artist's open window.

## For people

1. Open Patchy and choose Help > Set up AI Control.
2. Click Copy to Clipboard.
3. Paste the text into your AI assistant (Claude Code, Codex, Cursor, or another
   tool that supports MCP) and send it.

The assistant reads this document, registers the connector, installs the skill,
and shows you a small test drawing when it is done. Nothing else is required.
The rest of this page is written for the assistant, and for anyone who prefers
to do the steps by hand.

## For the AI agent doing the setup

You are configuring yourself, not Patchy. Do exactly three things and nothing
more:

1. Register the connector as a stdio MCP server named `patchy`. Add it beside
   any existing servers. Do not change, remove, or reorder other servers or
   settings.
2. Install the skill by copying the whole `patchy-control` folder, keeping its
   name, into your skills directory. Skip this if your tool has no skills
   feature; the connector works without it.
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

The connector takes no arguments and needs no Python or Node runtime. The client
starts and stops its own background workspace; restarting the connection closes
unsaved documents.

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
`.agents/skills/patchy-control`).

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

1. Call `get_info`. It reports Patchy's version, capabilities, and the skill
   directory it found.
2. Call `get_help` with `topic: "api"` and `get_state`.
3. Create a small document, draw a few strokes, and inspect the `get_preview`
   image.

If `get_info` fails, the connector path is wrong or the file is missing. If the
skill directory is empty in `get_info`, the `ai/patchy-control` folder is
missing from the installation.

## Protocol notes

Supported protocol versions: MCP 2025-11-25 and 2025-06-18, over stdio.
Messages use JSON-RPC IDs; stdout carries protocol messages and diagnostics go
to stderr. Requests are serialized. Cancellation stops JavaScript and its
timers; a native operation already in progress reaches its next interruption
boundary. The server exposes no HTTP listener and does not connect to hosted
chat services.

Online copy of this page:
https://github.com/SethRobinson/Patchy/blob/main/agent-kit/patchy-control/references/setup.md
