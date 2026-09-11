# Layers panel

Read this before changing layer rows, layer thumbnails, panel click selection, the folder disclosure arrow, or the visibility eye. Generic item-widget row rules (selection painting, transparent containers, `bind_widget_text`) stay in [ui-conventions.md](ui-conventions.md).

## Row styling

Layer rows use dynamic properties and application rules such as `QWidget#layerRowWidget[layerRowSelected="true"]`; repolish after property changes. The `layerTargetActive` pattern is the reference. `ui_layer_row_selected_highlight_paints` pins the rendered colors.

## Thumbnails

In document-mapped mode (the zoom preference below turned off), layer-panel previews show the whole document, shaped like it and centered in a fixed slot, the way Photoshop does: a 1200x280 document produces a 28x7 pixmap in the 30x30 label, and a layer smaller than the canvas appears at its own position inside that rect with checkerboard around it, so the thumbnail says where the pixels are and not only what they are. Content outside the canvas is clipped away. `thumbnail_tile_size` and `thumbnail_preview_space` in `src/ui/main_window_layer_panel.cpp` do the fitting and the document-space mapping; the content, mask, vector-mask, and Smart Filter mask previews all go through them, so the previews in one row share a shape. Letterboxing a square slot instead would surround an opaque wide layer with checkerboard, and checkerboard means alpha. The folder, text, and adjustment thumbnails are the deliberate exception; they are glyphs drawn against fixed 28px coordinates, so they stay square. Because the tile depends on the canvas extent, `LayerThumbnailCacheEntry` keys on it alongside the layer revision and `refresh_layer_thumbnails` restamps every row when it changes: growing the canvas reshapes a small layer's thumbnail without moving its revision. `layerTargetActive` frames the slot rather than the pixmap, which keeps switching the edit target a property flip with no thumbnail rebuild. `ui_layer_thumbnails_preview_the_whole_document` pins all of this. The `view/zoomLayerThumbnailsToContent` preference (default on, Preferences > Application) swaps the document space for a per-layer crop: the visible-alpha extent (revision-cached in core, so canvas-sized painted buffers zoom to their strokes), else the declared pixel bounds, else the raster mask bounds. `MainWindow::layer_thumbnail_crop` gates it and every preview in a row (content, mask, vector mask, Smart Filter mask) shares the crop so the row keeps one shape; glyph thumbnails are still exempt. Toggling the preference clears `layer_thumbnail_cache_` and rebuilds, since the mode is a shape input the revision keys do not track. `ui_layer_thumbnails_zoom_to_content_preference` pins the zoomed mode.

## Rebuilds and absent rows

Layer-list rebuilds must keep `refresh_layer_list`'s three-pass order (configure parentless items, insert all items, then attach row widgets); interleaving inserts with item mutation or widget attachment is quadratic in row count. Details and the measured numbers live in [performance.md](performance.md).

During guarded automation, script-originated rebuilds call `refresh_layer_list(true)` to capture old row widgets before clearing the list and deliver their deferred deletion after detachment. Manual callbacks and editable pauses use ordinary deferred deletion to preserve the current input receiver's lifetime. Panel scrolling, filtering and folder disclosure remain available during work. `ui_mcp_layer_rows_stay_bounded_during_long_script` pins the live-widget bound during execution.

The layer list may omit rows entirely: collapsed folders and the Layers panel name filter (`layerNameFilterEdit`) both rebuild without rows for excluded layers. Never assume every document layer has a row, and never introduce a "row exists but hidden" state; absent rows are the single not-shown state all consumers are hardened for. While the name filter is active, `LayerListWidget::set_drag_blocked` refuses drag reordering because a reorder would silently move filtered-out layers; each refused attempt reports through `show_status_error` and leftover held-button moves are swallowed so the base view cannot start a drag-selection sweep.

## Click selection

Layer-row click selection (`LayerListWidget::eventFilter` for row widgets, `viewportEvent` for bare viewport; keep the two branches in step): plain click selects one layer (collapse from a multi-selection is deferred to release so drags work), Ctrl-click toggles the row, Shift-click selects `currentRow()`..target replacing the selection, and Ctrl+Shift-click selects the same range but adds it to the existing selection (`select_range_to_item`'s `additive` flag, Explorer/Photoshop style). The Ctrl branch is tested first, so a Ctrl-click on a thumbnail always loads the layer pixels as a selection, with or without Shift.

The Move tool supports Shift+click and Ctrl+click on canvas to toggle the clicked layer whether Auto-Select is on or off (Command replaces Ctrl on macOS). Toggles commit on release below Qt's drag threshold. With Auto-Select on, a plain click selects only the clicked leaf, including a member of the current selection or a selected folder; collapse is deferred to release so dragging a selected member still moves the whole set. Shift-drag adds an unselected target before moving the enlarged selection, keeps an already-selected target selected, and constrains movement to an axis. Ctrl-drag always draws a layer-selection rectangle, including over artwork and with Auto-Select off. With Auto-Select on, dragging empty space or the pasteboard also draws a rectangle; a position-locked Background counts as empty space. "Empty space" means outside every movable layer's Move rect (`move_layer_outline_bounds`: the opaque raster extent, or a text layer's frame), not just off every opaque pixel: `CanvasWidget::topmost_move_layer_at` runs three passes (September 2026, Photoshop parity), a visible pixel under the point first, then a selected layer whose rect contains the point (the box the user is looking at beats a larger unselected rect above it), then the topmost layer whose rect contains it, so a press on a transparent pixel inside a layer's outline grabs that layer. The hover outline and the lock status message share the same lookup; the right-click layer menu deliberately still lists only layers with real pixels under the pointer. Ctrl bypasses passive transform handles, while rulers, guides, panning, and active transform sessions retain priority. Pinned by `ui_move_tool_grabs_transparent_pixel_inside_layer_rect` and `ui_move_tool_prefers_selected_layer_rect_over_topmost_rect`.

Rectangle intent and Shift-add mode latch at press. A plain rectangle replaces the selection; Shift adds. Matching runs once on release through the const layer tree, using cached Move outline bounds (opaque raster extent or text rect), clipped to the document. Any positive overlap selects an eligible leaf, including occluded leaves and children of collapsed folders; hidden, zero-opacity, position-locked, and non-movable layers are skipped with inherited group restrictions. The current active layer stays active if it remains selected; otherwise the topmost match becomes active. Empty clicks, empty rectangles, and toggling the last selected layer keep the existing selection. Escape, focus loss, tool/document changes, and edit locking discard pending selection gestures. Layer selection changes neither pixel selections nor content history.

The canvas requests panel selection changes through `CanvasWidget::set_layer_selection_requested_callback` -> `MainWindow::select_layers_in_layer_list` (the panel stays the source of truth and pushes the result back via `set_selected_layer_ids`); `activate_layer`'s single-id path still collapses to one row by design. The host reveals selected children of collapsed folders and clears a name filter that would hide selected layers. Rectangle drawing and pending modifier clicks live in `canvas_widget_move.cpp`; tests use the `ui_move_` filter.

Range selection normalizes selected ancestors with one const tree traversal
(`root_drop_layer_ids`), preserving requested order and rejecting missing ids.
It never compares every selected pair by repeatedly searching the tree.
Thumbnail target styles repolish only when their active state changes. Selection
updates use the delayed **Selecting layers...** canvas processing message when
control/row refresh takes long enough; fast selections show no overlay. The shared
selection handler and single-layer reveal path report the selected layer count
in the status bar, covering canvas clicks, rectangle selection, and panel
selection. A selected folder includes itself and every descendant, including
nested folders and hidden, locked, collapsed, or filtered-out layers. A selected
parent and child never count a layer twice. One const tree traversal computes the
count without changing the row selection or content/history state. Expanding a
folder therefore cannot change the count for the same selected trees.
`ui_layer_selection_count` covers nesting, overlapping selections, filtering,
empty folders, script selection, and the optional Little-Everywhere fixture.

## Move-tool layer menu

A right-click on the canvas with Move active opens `canvasMoveLayerContextMenu`
on release. It lists the hit leaf layers from top to bottom, including occluded
layers and children of collapsed or filtered folders. Folder paths distinguish
nested names. Picking a row replaces the layer selection; **Select All Layers
Here** appears for multiple hits and selects them with the topmost active. This
works with Auto-Select off. Locks do not prevent explicit selection.

Hit testing reads the const tree once, using raster alpha and masks or the text
rectangle. Hidden and zero-opacity trees, transparent pixels, and masked-out
folder contents are excluded. Empty canvas space opens no menu. Selection goes
through the same panel callback as rectangle selection, revealing its rows and
updating the count without editing pixels or history.

Small pointer jitter does not pan. Crossing Qt's drag threshold commits to
right-button panning, even if the pointer returns to its starting position.
Rulers, tablet-button actions, Space/middle-button panning, and active transform
sessions keep their existing handling. Tool/document changes and edit locks
close the popup; focus loss cancels a pending click. The `ui_move_layer_menu`
tests cover selection, eligibility, panel reveal, and gesture/lifetime behavior.

## Disclosure arrow, double-click, visibility eye

The folder disclosure arrow (`layerFolderDisclosureButton`) toggles one folder, Alt-click also toggles the folders nested inside it, and Ctrl+Alt-click sets every folder in the document to the clicked folder's toggled state (`MainWindow::toggle_all_layer_folders_expanded`, Photoshop's binding). `QToolButton::clicked` carries no modifiers, so a `ClickModifierRecorder` event filter stores them off the button's own mouse events; the handler defers through `QTimer::singleShot(0)` because the toggle rebuilds the rows. Runtime expand state is the session's `collapsed_layer_groups` set; layer metadata only seeds it at open.

Double-clicking a row opens its editor (layer styles, adjustment settings, or shape appearance), except on the content thumbnail: `LayerListWidget`'s content-thumbnail double-click callback routes to `MainWindow::zoom_canvas_to_layer_content`, which fits the canvas view to the layer's alpha-trimmed bounds (`move_layer_outline_bounds`, falling back to `layer_render_bounds`, which unions a folder's descendants) via `CanvasWidget::zoom_to_document_rect`. Adjustment thumbnails still open their settings; they have no canvas extent. `ui_layer_thumbnail_double_click_zooms_canvas_to_layer` pins it.

The visibility eye (`layerVisibilityCheck`) does not toggle through its QToolButton on real mouse input: `LayerListWidget::eventFilter` eats the press first. A plain press toggles on press and starts a visibility sweep (dragging along the eye column paints the first toggle's state across crossed rows, skipping disabled eyes; `setSelection` is suppressed while sweeping so the drag cannot rubber-band the selection). An Alt press calls `MainWindow::isolate_layer_visibility`, which hides every other layer and restores the per-session snapshot on the second Alt-click; any outside visibility change invalidates the snapshot and the next Alt-click isolates fresh. Both handlers defer through `QTimer::singleShot(0)` because folder toggles rebuild the rows, and the button's `toggled` connection remains for programmatic `click()` (tests rely on it). Visibility stays non-undoable.

## Animation preview

The film button in the panel's action row (`layerAnimationButton`) toggles the Animation Preview tool window (`AnimationPreviewWindow`, `src/ui/animation_preview_window.{hpp,cpp}`; the TilePreviewWindow pattern: Qt::Tool with custom chrome, every dismissal through `done()`, `WA_DeleteOnClose` behind a `QPointer`). Play cycles the top-level layers that were visible at start, top to bottom (the animated GIF export's frame order), hiding every other top-level layer; a trailing "0.25s" layer-name token overrides the per-frame delay and the panel's spin edits the shared `saveOptions/gifFrameDelayCs` default the export dialog reads. Stop, closing the panel, a tab switch, or closing the document restores the visibility captured at start: MainWindow calls `stop_playback_for` in `activate_document_canvas` (while the outgoing document is still active) and early in `close_document_session` (before the save-changes prompt, so a Save never writes a preview frame's visibility). Playback mutates visibility directly, pushes no undo entries, never marks the session modified, and updates rows per frame through the cheap in-place `sync_layer_row_visibility_indicators`, never a per-frame `refresh_layer_list` rebuild; the full refresh runs once at stop. Pinned by `ui_animation_preview_plays_visible_layers_and_restores` and `ui_animation_preview_stops_on_tab_switch_and_close`.

The panel's "Selected layers" row edits the name tokens: Set Time renames the selected (else active) layers to end with the row's own time spin (replacing any existing token, via `gif::strip_layer_name_delay_token` plus `format_delay_seconds_token`), Remove strips the token, and a name that is nothing but a token keeps it (empty layer names help nobody). Both go through `MainWindow::set_selected_layers_frame_time` as one undo snapshot ("Set frame time"/"Remove frame time"), and the panel stops playback (with restore) before invoking it so the snapshot never captures a preview frame's visibility; a no-op click pushes nothing and reports through `show_status_error`. Pinned by `ui_animation_preview_sets_and_removes_frame_time_names`.

## Drag to another document

Dragging rows out of the panel onto another open document copies the layers there
(Photoshop's duplicate-by-drag). The drag's mime data carries the layer ids
(`application/x-patchy-layer-ids`) plus the source session as `pid:session_id`
(`application/x-patchy-layer-source-session`, written by `LayerListWidget::mimeData` from the
id `refresh_layer_list` stamps on the list) because layer ids restart per document, and a
drag from another Patchy process never matches. `startDrag` offers `Copy | Move` so the
cursor shows the copy badge over a foreign document; the panel's own reorder and the
footer buttons keep forcing Move. Drop targets are every session canvas (a tab page or a
float) and the document tabs, all handled by
`MainWindow::handle_cross_document_layer_drag_event` from the app event filter, ahead of
the tab-bar tear-off branch: the tab bar has `setAcceptDrops` so it becomes the DnD
receiver, and non-layer mime stays unaccepted so file drops still propagate to the tab
widget. A drag over its own document, a missing or foreign source token, a gap between
tabs, or the preview-dialog edit lock refuses the drop (no drop cursor, and the file-drop
handlers never see a layer drag); the hovered tab lights through the shared tab-strip
highlight (`show_tab_strip_highlight`, the float-docking overlay).

The drop returns before any document work: the payload (ids, session ids, drop point,
Shift) is copied out and `duplicate_layers_to_session` runs from `QTimer::singleShot(0)`
(the source panel's `QDrag::exec` is still on the stack and the copy rebuilds its rows).
`copy_layers_between_sessions` (main_window_layer_ops.cpp) is the core, shared with the
dialog and scripting paths: it builds the payload Edit > Copy builds (root ids, referenced
smart-object sources, Smart Filter records, pattern tiles), validates the Smart Filter
caches against the target, runs the caller's `before_mutation` hook (the UI pushes the
target's "Duplicate layer" snapshot there), clones every root with
`clone_layer_tree_with_document_ids` BEFORE inserting anything (there is no
session-targeted undo to roll back a half-inserted stack), keeps the source names (a
collision earns the copy suffix), applies one shared placement offset, re-bakes vector
rasters against the target canvas, and inserts the stack directly above the target's
active layer in source order, making the topmost copy active. Placement: a canvas drop
centers the copied set's movable extent on the drop point; Shift-drop, a tab drop, the
dialog and scripts keep the source coordinates when the documents share dimensions and
center on the target canvas otherwise. A root whose Smart Filters depend on position keeps
its coordinates (the adopted cache would go stale; re-rendering it is a follow-up). Linked
masks move inside `translate_moved_layer_metadata`; unlinked raster and vector masks are
shifted explicitly so the copy stays one unit. The UI wrapper then refreshes the target
canvas, activates the target session and selects the copies. `ui_layer_drag_*` in
tests/ui/layer_panel_organization_tests_cross_document.cpp and
`ui_layer_drag_to_float_canvas_centers_at_drop_point` in tests/ui/float_window_tests.cpp pin
it; `send_layer_drop_to_widget` synthesizes the drag.

Inside the panel, an Alt-drop duplicates instead of moving (Photoshop's Alt-drag):
`LayerListWidget::dropEvent` records `LayerDropRequest::copy` from the event's
modifiers (the enter/move handlers report CopyAction so the cursor shows the badge), and
`MainWindow::duplicate_layers_for_drop` clones each dragged root directly above its
source, then runs the ordinary `move_layers_for_drop` on the CLONES, so the originals
never move; the copies become the selection under one "Duplicate layer" snapshot.
`ui_layer_alt_drag_duplicates_in_panel` pins it.

The same core backs Duplicate Layer to Document... (`layerDuplicateToDocumentAction`, hotkey
id `layer.duplicate_to_document`, in the layer context menu and added to the window itself
because the Layer menu's row count is pinned): its dialog (`duplicateLayerToDocumentDialog`)
takes a name for a lone copy (`duplicateLayerNameEdit`) and a destination
(`duplicateLayerTargetCombo`: every other open document, then New Document, a fresh
session the source's size and print resolution), always with keep-position placement.
The scripting API's `layer.duplicate(targetDocument)` routes through
`ScriptEngineHost::duplicate_layers_to_session`, so the run's own snapshot covers the
target and no activation happens. `ui_duplicate_layer_to_document_dialog_copies` and
`ui_script_layer_duplicate_to_document` pin them.
