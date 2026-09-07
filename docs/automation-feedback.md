# Automation feedback and image resize

Visible MCP and unattended CLI runs publish completed edits at most every 50 ms.
`ScriptEngineHost::refresh_script_view` flushes document dirt before repainting,
including structure and vector panels. Pixel/structure notifications enqueue their
current change before pumping. Native brush samples publish their accumulated
dirty rectangle at safe boundaries through `paint_script_stroke`'s progress
callback. Painting math and seed order remain unchanged. Hidden work retains
coalesced refresh without forced screen repainting.

The checkable Slow button beside Stop enables `patchy.ui.slowMode`. It defaults
off for each workspace, stays selected between requests and reconnects, and can
change during a run. It shares state between the MCP and visible CLI controls.
Each completed native stroke or undoable document edit then gets a separate Undo
step and a short visible hold. Normal mode groups edits per script and document.
Enabling Slow starts a new group at the next edit boundary; disabling it groups
subsequent edits again. Earlier grouped work stays grouped. Existing history count
and memory limits still apply; large paintings cannot retain unlimited strokes.
The display hold ends early on Stop or when Slow is turned off. Simulated paint
time is independent, so airbrush buildup and smoothing output stay unchanged.
Headless runs cannot enable Slow and retain normal speed and grouped Undo.
The setter rejects enabling it without a visible workspace. MCP state includes
`slowModeAvailable`; offscreen UI tests exercise visible-mode behavior in their
owned windows without setting the production `PATCHY_HEADLESS` flag.

`prepare_mutation` tracks both touched documents and the current history group.
Completion notifications close a Slow group once, including vector changes.
Native in-stroke dirt passes `completed=false`; only the finished stroke closes
its group. Multiple notifications for one edit cannot create extra checkpoints.
Reads, previews, brush-library writes and view changes do not create Undo steps.

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
so they cannot crowd Stop and Slow out of a narrow status bar. Both controls are
inside the input guard's allowed widget subtree.

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
