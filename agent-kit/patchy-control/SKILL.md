---
name: patchy-control
description: Create and edit images, layered documents, and pixel art in Patchy using its local MCP connector or JavaScript API.
---

# Control Patchy

Use the configured Patchy tools. At the start of a Patchy task, call `get_info`,
then `get_help` with `topic: "workflow"` and `topic: "api"`, then `get_state`.
Follow that installation's current workflow and read additional help topics as
needed. Fetch requests sequentially.

The connected Patchy's help is authoritative for its API and editing behavior.
Do not use old copied references as current documentation. Reconnect after a
Patchy upgrade, then fetch the help again; save work before disconnecting.

If tools are unavailable, distinguish an unconfigured connector from a connection
that needs restarting. Use Patchy's Help > Set up AI Control instructions and
preserve unrelated client settings. Do not claim a connection was tested until
a tool call succeeds.

For a requested CLI workflow, read `references/workflow.md` and
`references/patchy.d.ts` from the control kit beside the current Patchy
installation. These files stay with Patchy and are not installed with this skill.
