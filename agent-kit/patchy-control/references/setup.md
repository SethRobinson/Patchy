# Setup

The desktop package includes `patchy-mcp`, the local connector, and this skill.
No Python or Node runtime is required. Configure a stdio MCP server whose command
is the absolute connector path. The client starts and stops its own background
workspace. Restarting the connection closes unsaved documents.

| Platform | Connector | Skill folder |
|---|---|---|
| Windows | `<Patchy folder>/patchy-mcp.exe` | `<Patchy folder>/ai/patchy-control` |
| macOS | `Patchy.app/Contents/MacOS/patchy-mcp` | `Patchy.app/Contents/Resources/ai/patchy-control` |
| Linux prefix install | `<prefix>/bin/patchy-mcp` | `<prefix>/share/patchy/ai/patchy-control` |
| Linux Flatpak | command `flatpak`, arguments `run`, `--command=patchy-mcp`, `com.rtsoft.patchy` | `/app/share/patchy/ai/patchy-control` inside the sandbox |

For example, in PowerShell:

```powershell
codex mcp add patchy -- 'C:\Program Files\Patchy\patchy-mcp.exe'
```

Generic client configuration (adapt the outer settings format to your client):

```json
{
  "mcpServers": {
    "patchy": {
      "command": "C:\\Program Files\\Patchy\\patchy-mcp.exe",
      "args": []
    }
  }
}
```

Set the server's working directory to the artwork output folder if your client
supports it. Otherwise use absolute paths in scripts. Patchy does not infer the
agent's working directory from its conversation.

For Codex, copy the entire assembled `patchy-control` directory into
`~/.agents/skills/` (or a project's `.agents/skills/`). Other clients use their
own skill installation locations. Restart the client if it does not discover
the skill. Installing a skill and connecting an MCP server are separate steps.

From a source checkout, build the desktop preset first: the assembled skill is
in `build/<preset>/ai/patchy-control`. CMake copies the current API and guide into
its references folder; do not install the unassembled `agent-kit` source folder.

For Flatpak, the connector can read the skill through `get_help`. To copy it out
for skill installation, use `flatpak run --command=cp com.rtsoft.patchy -R
/app/share/patchy/ai/patchy-control <destination>` with a destination visible to
the sandbox. File access follows the installed Flatpak permissions.

Verify setup with `get_info`, `get_help` (`topic: "api"`), and `get_state`.
Then ask the agent to create a small sprite and inspect its preview.

Supported protocol versions: MCP 2025-11-25 and 2025-06-18, over stdio.
Messages use JSON-RPC IDs; stdout contains protocol messages and diagnostics go
to stderr. Requests are serialized. Cancellation stops JavaScript and its timers;
a native operation already in progress reaches its next interruption boundary.
The server exposes no HTTP listener and does not connect to hosted chat services.

References: [Codex MCP](https://learn.chatgpt.com/docs/extend/mcp),
[skills](https://learn.chatgpt.com/docs/build-skills).
