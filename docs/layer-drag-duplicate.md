# Layer drags to other documents, Duplicate Layer and layer export

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
id `layer.duplicate_to_document`, in the layer context menu's Export submenu and on the window itself since
the Layer menu's row count is pinned): its dialog (`duplicateLayerToDocumentDialog`) takes
a name for a lone copy (`duplicateLayerNameEdit`) and a destination
(`duplicateLayerTargetCombo`: every other open document, then New Document, a fresh
session of the source's size and print resolution), always keep-position. The scripting
API's `layer.duplicate(targetDocument)` routes through
`ScriptEngineHost::duplicate_layers_to_session`, so the run's own snapshot covers the
target and no activation happens. `ui_duplicate_layer_to_document_dialog_copies` and
`ui_script_layer_duplicate_to_document` pin them.

## Layer export (Quick Export as PNG, Export As...)

The row and pixel-thumbnail menus' Export submenus (#462) hold `layerQuickExportPngAction`
and `layerExportAsAction` (hotkey ids `layer.quick_export_png`, `layer.export_as`, no
default keys, on the window for that reason). Both run `MainWindow::export_selected_layers`
(main_window_files.cpp) over `selected_or_active_layer_ids()`: each layer renders alone
(`render_layer_isolated`, a group as its composite, a hidden layer as if shown) and is
written by `write_flat_image_file` (atomic) to a file named after it
(`image_sequence_file_names` layer-name mode: unsafe characters become `_`, repeats get
" 2", " 3"). Layers without visible pixels are skipped and the status bar says so; a lone
empty layer stops before any dialog. One layer asks for a file name (the layer's name
filled in); Quick Export with several asks for a folder (`get_existing_directory`; the
browser build writes to a MEMFS folder and downloads each file). Export As always uses the
save dialog for the format (several layers: its folder, layer names) and then that
format's options dialog. Quick Export trims each PNG to its pixels; Export As keeps the
canvas so the dialog's Trim and Resize apply as for a document. The folder is remembered as
`layerExport/lastFolder`. Tests: `ui_layer_quick_export_png_writes_trimmed_named_files`,
`ui_layer_export_as_uses_chosen_format_and_options`,
`ui_layer_thumbnail_menu_offers_layer_export`.
