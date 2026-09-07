# Automation feedback and image resize

Visible MCP and unattended CLI runs publish completed edits at most every 50 ms.
`ScriptEngineHost::refresh_script_view` flushes document dirt before repainting,
including structure and vector panels. Pixel/structure notifications enqueue their
current change before pumping. Native brush samples publish their accumulated
dirty rectangle at safe boundaries through `paint_script_stroke`'s progress
callback. Painting math, seed order, and the one-snapshot-per-run contract remain
unchanged. Hidden work retains coalesced refresh without forced screen repainting.

`patchy.ui.present(delayMs)` forces a frame and services events for an optional
0..1000 ms hold. It feeds the inactivity watchdog without mutating history or file
state. Existing synchronous/callback gates defer JavaScript timers during the
hold. Native document edits must be committed before calling it: changing a
private JavaScript array does not change a layer until `setPixels` uploads it.
Visible examples may use `watch=true` for intentional pacing; background work
should omit it. Repainting cannot show computation still taking place in a client.

MCP keeps its Stop button visible while connected, disabled outside a mutating
request. Idle means waiting for the next request; stopping the assistant between
requests belongs to the client's own Stop control. Long labels have bounded width
so they cannot crowd Stop out of a narrow status bar.

Visible unattended scripts use a separate `McpActivity` instance in script mode,
with `scriptActivity` and `scriptStopButton` identifiers. Its input guard and Stop
callback belong to that script, independently of any idle MCP connection. It
releases input on finish, error or cancellation. Creating an interactive script
canvas dismisses this guard, preserving game-window input. Headless scripts show
no activity widget. Normal interactive scripts retain their existing stop panel.
The script activity widget is parent-owned with a guarded pointer in the host;
the status bar may be destroyed before the host during window teardown.

`MainWindow::resize_document_image` is shared by Image Size and `doc.resizeImage`.
A background worker copies the source document and resamples only that copy.
`wait_for_processing_operation` keeps the existing delayed Processing overlay
alive against the unchanged source. The worker joins before replacement, and a
cancelled script discards its result. Stop does not interrupt the native resampler
mid-allocation; completion waits for that current compute to finish. GUI Image Size
retains its resolution, smart-object refresh and view/channel restoration steps.
The existing preview edit lock blocks competing workspace edits and makes MCP
report busy throughout the resize wait.

Coverage: `ui_mcp_progressive_edits_and_present_keep_history` passively observes
intermediate canvas paints and explicit frames, then verifies one-step Undo.
`ui_script_visible_unattended_stop_and_resize_processing` checks CLI Stop and
cleanup, actual 10000-square GUI resampling with the Processing indicator, Undo,
cancelled resize preservation, and presentation from interactive scripts. Native stroke parity, MCP cancellation,
scripting, image-size, channel and smart-object tests remain required checks.
