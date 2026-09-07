# Automation feedback and image resize

Pause/Resume beside Stop suspends visible MCP/CLI automation after the current
native edit. `patchy.ui.paused` shares the control and resets at run end. While
Pausing appears, Stop remains available. Once Resume appears, manual drawing,
layer edits, Undo, document changes and settings are available. A nested event
loop yields CPU and feeds the inactivity watchdog. Script callbacks remain
deferred; simulated paint time does not advance. Stop, cancellation and
disconnect leave the pause cleanly. Hidden runs
and ordinary interactive scripts reject enabling it. The active MCP request stays
busy; Resume is a window control, not a second concurrent script request.

The activity input guard permits window chrome, the zoom percentage editor,
canvas wheel/pinch navigation, panel scrolling/filtering/disclosure, informational
dialogs, menu browsing, view commands, and middle/right/Space-drag pan.
Pan calls the view helpers directly so it cannot enter a painting tool's mouse
handlers. Conflicting edit commands display a localized explanation asking the
user to pause. Preferences can be inspected while working; applying them requires
closing the dialog and pausing first. The menu bar stays enabled because it also
contains custom title-bar dragging and window buttons. Activation by mouse,
shortcut or menu mnemonic is checked separately. Quit requires Stop first.
The guard follows QObject ownership across popup windows; QWidget ancestry alone
does not include menus and dialogs in another window.

`ScriptApiCall` scopes defer editable pauses until native API locals and temporary
tool state have unwound. Never retain session/layer pointers across an editable
pause. Stroke batches also park between completed strokes, resolve the target
again, and reject missing or incompatible layers before the next stroke. Other
wrappers resolve their owning IDs on each call. Resume waits for unfinished manual
pointer/transform/crop/text gestures to finish. Manual document changes split the
script history group, keeping manual Undo steps separate from subsequent automated
edits. Navigation alone does not split history. This also applies between async
callbacks. Cached JS geometry remains the script author's responsibility: inspect
again after a pause when an operation depends on the artist's latest geometry.

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
inside the input guard's allowed widget subtree, along with Pause/Resume.

Script-originated layer-panel rebuilds during guarded automation deliver deferred deletion
only to their detached old row widgets. A long JavaScript evaluation otherwise
retains every retired generation until it returns to its outer event loop, causing
memory and repaint costs to climb with each edit. Manual row-click lifetimes and
unrelated deferred objects retain their normal Qt delivery order.

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
