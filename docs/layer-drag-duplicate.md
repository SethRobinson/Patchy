# Layer drags to other documents and Duplicate Layer

The rest of the Layers panel, Delete Layer included, is in [layer-panel.md](layer-panel.md).

## Drag to another document

Dragging rows onto another open document copies the layers there. The drag's mime data carries the layer ids (`application/x-patchy-layer-ids`) plus the source session as `pid:session_id`
(`application/x-patchy-layer-source-session`, from the id `refresh_layer_list` stamps on the list): layer ids
restart per document, and a drag from another Patchy process never matches. `startDrag` offers `Copy | Move` so the
cursor shows the copy badge over a foreign document; the panel's own reorder and the footer buttons force Move. Drop targets are every session canvas (a tab page or a float) and the document tabs,
all handled by `MainWindow::handle_cross_document_layer_drag_event` from the app event
filter, ahead of the tab-bar tear-off branch: the tab bar has `setAcceptDrops` so it
becomes the DnD receiver, and non-layer mime stays unaccepted so file drops still reach
the tab widget. Its own document, a missing or foreign source token, a gap between tabs, or the
preview-dialog edit lock refuses the drop (file-drop handlers never see a layer drag); the
hovered tab lights through `show_tab_strip_highlight`.

The drop returns before any document work: the payload is copied out and
`duplicate_layers_to_session` runs from `QTimer::singleShot(0)` (the source panel's
`QDrag::exec` is still on the stack and the copy rebuilds its rows).
`copy_layers_between_sessions` (main_window_layer_ops.cpp; `copy_layers_between_documents`
is its Document-level core, shared with Files as Layers) backs the dialog and scripts
too: it builds Edit > Copy's payload, validates the Smart Filter caches against the
target, runs the caller's `before_mutation` hook (the UI pushes the target's "Duplicate
layer" snapshot there), clones every root with `clone_layer_tree_with_document_ids`
BEFORE inserting anything (no session-targeted undo can roll back a half-inserted
stack), keeps the source names (a collision earns the copy suffix), applies one shared
placement offset, re-bakes vector rasters against the target canvas, and inserts the
stack directly above the target's active layer in source order, topmost copy active. Placement: a canvas drop
centers the copied set's movable extent on the drop point; Shift-drop, a tab drop, the
dialog and scripts keep the source coordinates when the documents share dimensions, else
center on the target canvas. A root whose Smart Filters depend on position keeps
its coordinates (the adopted cache would go stale). Linked
masks move inside `translate_moved_layer_metadata`; unlinked raster and vector masks shift
explicitly so the copy stays one unit. The UI then activates the target and selects the copies. Pinned by `ui_layer_drag_*` (layer_panel_organization_tests_cross_document.cpp)
and `ui_layer_drag_to_float_canvas_centers_at_drop_point` (float_window_tests.cpp);
`send_layer_drop_to_widget` synthesizes the drag.

Duplicate Layer (`layerDuplicateAction`, the footer button and its drop target) runs
`MainWindow::duplicate_layers`: the copies land as one block directly above the topmost
selected root, in its parent and in source order, and become the selection. `ui_duplicate_layer_inserts_copies_above_source` pins it.

An Alt-drop in the panel duplicates instead of moving: `LayerListWidget::dropEvent` records `LayerDropRequest::copy` from the event's modifiers
(enter/move report CopyAction for the badge), and `MainWindow::duplicate_layers_for_drop` clones each dragged root above its source, then
runs `move_layers_for_drop` on the CLONES, so the originals never move; the copies become
the selection under one snapshot. `ui_layer_alt_drag_duplicates_in_panel` pins it.

The same core backs Duplicate Layer to Document... (`layerDuplicateToDocumentAction`, hotkey
id `layer.duplicate_to_document`, in the layer context menu and on the window itself since
the Layer menu's row count is pinned): its dialog (`duplicateLayerToDocumentDialog`) takes
a name for a lone copy (`duplicateLayerNameEdit`) and a destination
(`duplicateLayerTargetCombo`: every other open document, then New Document, a fresh
session of the source's size and print resolution), always keep-position. The scripting
API's `layer.duplicate(targetDocument)` routes through
`ScriptEngineHost::duplicate_layers_to_session`, so the run's own snapshot covers the
target and no activation happens. `ui_duplicate_layer_to_document_dialog_copies` and
`ui_script_layer_duplicate_to_document` pin them.
